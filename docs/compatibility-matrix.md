# Local Compatibility Matrix

## Baseline matrix

| Item | Supported or observed baseline | Evidence status |
|---|---|---|
| Product branch | `embed-labs` only | Verified |
| Issue baseline commit | `03a6abd1c696d34acae685bdf0c340a9a471a837` | Verified |
| Product version | 20.0.1 | Verified |
| Recorded Qt Creator merge point | `11ba5cec09dce75db4bc948d98055e338ff59576` | Verified |
| Qualified product Qt | Homebrew 6.11.0 | Clean Release build and GUI smoke verified |
| Older Qt | 6.8.3 macOS | Build blocked by missing AGL in Xcode 26 SDK |
| Host architecture | macOS arm64 | Verified |
| Compiler | Apple Clang 17.0.0 | Verified |
| CMake | 4.2.3 | Verified |
| Ninja | 1.12.1 | Verified |
| Python | 3.14.4 | Verified, not yet qualified for every tool |
| qbs submodule | `986ebc8df121bb75b05f7ee904e892ff25272105` | Recorded |
| qlitehtml submodule | `a26decdb0f7f653b66c7343daede5067a26cedcf` | Recorded |
| googletest submodule | `52eb8108c5bdec04579160ae17225d66034bd723` | Recorded |
| perfparser submodule | `f17236ab4aa261887cbc0a7ac366e8bf67a09af1` | Recorded |

## Product plugin profile

The product must use an explicit build allow-list. Unrelated upstream plugins
are hidden by not building them; their source remains available for future
local decisions and easier maintenance.

### Existing baseline profile

| Plugin | Current purpose | Product classification |
|---|---|---|
| Core | Qt Creator platform | Required |
| TextEditor | Editor framework and transitive support | Required by current dependencies |
| ProjectExplorer | Project and target framework | Required |
| CppEditor | Current product editor/debug dependency | Retain for baseline |
| Debugger | EasyBoard dependency | Retain while EasyBoard is enabled |
| QtSupport | Project/kit support | Required by current dependencies |
| ResourceEditor | Qt resource editing | Retain for baseline |
| RemoteLinux | EasyBoard deployment dependency | Retain while EasyBoard is enabled |
| QmakeProjectManager | Required by current Debugger plugin metadata | Required by baseline dependency |
| EasyBoard | Existing board discovery/deployment mode | Preserve; visibility review pending |
| EtherCATCore | EtherCAT services and extension points | Stage 1 verified |
| EtherCATProject | Offline project lifecycle and persistence | Stage 2 verified |
| EtherCATDevices | Offline ESI repository and immutable device data | Stage 3 verified |
| EtherCATWorkbench | EtherCAT mode, device tree, selection, master-side ESI insertion, supported-device drag-and-drop, and offline property pages | Stage 4 verified |
| EtherCATScan | Local Mock scan, topology comparison, and checked acceptance | Stage 5 verified |
| EtherCATDiagnostics | Local Mock state, WKC, DC, alarm, and performance views | Stage 6 verified |

### Phase-1 EtherCAT profile

All six planned EtherCAT feature and infrastructure plugins have entered the
local profile. This proves the plugin profile is assembled, not that every
Phase-1 requirement is complete. Editable Process Data, CoE Online Mock,
Startup, and DC are now verified, and the public derived-node kinds are
reserved. The existing Project-name command, the checked Target/Master name
command, and the TwinCAT-inspired Project/Target/Master General pages are now
verified. The local ESI Device Repository page now exposes verified batch
import, reload, cancellation, progress, and partial-failure reporting through
the public Devices Provider. An individual ESI catalogue device now exposes a
dedicated TwinCAT-aligned read-only General page for identity, offline
configuration coverage, import qualification, and source provenance. The
offline Master now exposes a TwinCAT-style `Add New Item...` workflow with an
ESI-only searchable selector, latest-revision default, optional previous
revisions, qualification gating, and Project-owned Undo/Redo. Supported ESI
catalogue devices can also be copy-dragged onto the active offline Master
through the same checked insertion path. The
visible EtherCAT-master EtherCAT page now exposes the documented NetId/action/frame
hierarchy with a real local topology view and explicit unavailable runtime
boundaries. The visible
TwinCAT-inspired process-data tree is now verified with stable selection and
details routing. Multiple open project roots now expose one explicit
inactive-root `Set as Active Project` command while retaining independent
selection and offline state. Real modular-profile data, additional UI coverage,
and the user-policy-deferred upstream rehearsal remain open.

### Hidden or excluded plugins

All other upstream plugins are excluded from the EtherCAT product build by
default unless a documented dependency or user requirement adds them. This is
a build/profile decision, not source deletion.

No upstream plugin source is approved for deletion in stage 0. A product-owned
plugin may only be deleted through a dedicated local issue that proves its
function is outside the product target and records migration or recovery.

## Feature compatibility

| Feature | Phase-1 status |
|---|---|
| TwinCAT-inspired device tree shell | Stage 4 verified |
| Multiple open project roots and active-project marker | Verified with one textual `Active project \| Offline` marker, inactive `Offline` roots, dynamic filter/drop-target handoff, and real close fallback |
| Set active project from Workbench | Verified for the inactive project-root context menu, stable-ID activation, active/non-Project exclusion, unchanged Selection/Details/Project snapshots, and no controller or online meaning |
| Inputs/Outputs/RxPDO/TxPDO tree branches | Verified in current Workbench issue |
| Modules/Channels tree branch | Explicit empty state verified; real modular data pending Devices/data-contract issue |
| Public process/PDO/module/channel node kinds | Core/API contract verified |
| Extensible offline property pages | Stage 4 verified |
| Configured-slave General and EtherCAT/Alias pages | Verified; unsupported fixed address, identification, port graph, and Advanced Settings remain explicit unavailable states |
| Target/Master structural-name command | Core/API, Project persistence, and both Target/Master General UIs verified |
| Offline-project General page | Verified with TwinCAT-aligned identity hierarchy, truthful offline summary, synchronized ProjectExplorer/Workbench naming, and Project Undo/Redo |
| Offline-target General page | Verified with TwinCAT-aligned target/version hierarchy, explicit unavailable runtime controls, and Project Undo/Redo |
| EtherCAT-master General page | Verified with TwinCAT-aligned identity hierarchy, explicit unsupported settings, offline summary, and Project Undo/Redo |
| EtherCAT-master EtherCAT page | Verified with TwinCAT-aligned NetId/actions/frame columns, current-snapshot offline topology, and explicit unavailable ADS/runtime/export/Sync Unit state |
| Offline Process Data/Startup/DC domain model | Verified in EtherCATData |
| Project persistence and Undo/Redo for those models | Verified in format version 2 |
| Editable Process Data page | Verified in current Workbench issue |
| CoE Online Mock and explicit Add to Startup | Verified in current Workbench issue; no SDO or controller access |
| Editable Startup page | Verified in current Workbench issue |
| Editable DC page | Verified in current Workbench issue |
| Offline EtherCAT project | Stage 2 verified |
| ESI repository | Devices storage/parser/provider, Workbench import/reload/cancel UI, and actionable empty guidance verified; local/offline only |
| Individual ESI catalogue-device General page | Verified with TwinCAT-aligned identity, configuration coverage, qualification/source details, explicit unavailable state, and no controller access |
| Master-side ESI device insertion | Verified with context-only `Add New Item...`, ESI search, latest/previous revision handling, qualification gating, stable IDs, and Project Undo/Redo |
| Supported ESI device drag-and-drop | Verified for private stable-ID CopyAction, exact active-Master targeting, append semantics, rejection boundaries, and Project Undo/Redo |
| Scan UI and topology comparison | Stage 5 verified with Mock provider only |
| WKC/DC/link diagnostics | Stage 6 verified with Mock provider only |
| Optional Scan/Diagnostics Provider state | Verified for absent, registered/unavailable, and available states with public `Local Mock` producer names and no installation inference |
| Zynq protocol | Explicitly out of scope |
| Real EtherCAT scan | Explicitly out of scope |
| ECPKG/ECFG/ETIR | Explicitly out of scope |
| ST/LD/FBD | Explicitly out of scope |

## EtherCATCore stage-1 qualification

| Check | Result |
|---|---|
| Focused Qt Creator plugin tests | 12 passed, 0 failed |
| Normal Release product build | Passed with 11-plugin allow-list |
| Enabled clean-settings startup | Passed; stable until intentional `SIGTERM` |
| Explicitly disabled startup | Passed with `-noload EtherCATCore` |
| Normal plugin shutdown | Passed through automatic plugin-test exit |
| Repeated process startup | Passed across focused test and two smoke runs |
| Direct upstream Core or app changes | None |
| Full product build with `WITH_TESTS=ON` | Blocked by existing EasyBoard test include defect |

## Workbench derived-node Core/API qualification

This issue reserves only stable selection and property-page routing identities.
It does not claim that the Workbench renders the new tree branches, that the ESI
parser supports modular profiles, or that module/channel values may be
fabricated when source data is absent.

| Check | Result |
|---|---|
| Failure-first contract build | Failed on all nine missing derived kinds as expected |
| Focused EtherCATCore contract tests | 17 passed, 0 failed |
| Existing `WorkbenchNodeKind` values 0 through 8 | Frozen and verified |
| Appended process/PDO/module/channel values 9 through 17 | Verified |
| Derived `PropertyPageContext` value semantics | Verified |
| EtherCAT plugin regressions | Core 17, Project 11, Devices 8, Workbench 12, Scan 7, Diagnostics 7 passed |
| Regression execution model | Each plugin suite passed in an isolated process; a combined process is not a valid Provider-isolation gate |
| CMake and qbs source lists | No source-list change required |
| Normal Release product build | Passed with 16-plugin allow-list |
| Enabled GUI startup | Passed for 5 seconds until intentional interrupt |
| User-visible tree or page change | None in this API prerequisite issue |
| Direct upstream Core, ProjectExplorer, or app changes | None; direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Not rerun; existing EasyBoard test include defect remains |
| qbs build | Not run; qbs executable is unavailable |

## Offline configuration data qualification

This local Core/API issue adds only UI-independent `EtherCATData` values and
algorithms. It does not claim project persistence or an editable UI.

| Check | Result |
|---|---|
| Focused EtherCATCore contract tests | 16 passed, 0 failed |
| Valid RxPDO/TxPDO direction and SM assignment | Passed |
| Automatic and explicit bit-offset process-image preview | Passed |
| Duplicate, overlap, width, unsupported, missing-SM errors | Passed |
| Sync Manager capacity validation | Passed |
| Startup order and raw-value validation | Passed |
| DC cycle and shift validation in nanoseconds | Passed |
| CMake and qbs source lists | Synchronized |
| EtherCAT plugin regressions | Core 16, Project 8, Devices 8, Workbench 9, Scan 7, Diagnostics 7 passed |
| Normal Release product build | Passed with 16-plugin allow-list |
| Product version inventory | All 16 allow-listed plugins present and recognized |
| Clean-settings GUI startup | Passed; stable for 5 seconds until intentional `SIGTERM` |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Project persistence | Verified by the later Project revision below |
| Editable UI | Pending Workbench issue |

## Project structural-node-name Core/API qualification

`ISSUE-API-PROJECT-NODE-RENAME-001` is an API prerequisite for later
TwinCAT-inspired Target/Master General pages. It deliberately adds no Workbench
widget or generic node-editing surface.

| Check | Result |
|---|---|
| Failure-first Project build | Failed first on the missing document `renameStructuralNode` method; the same test also required the absent public service call |
| Public command scope | Existing Target or Master stable ID only; Project and Slave kinds remain owned by their existing commands |
| Name normalization and rejection | Whitespace trim, empty-name rejection, unknown-ID rejection, Project/Slave rejection, and normalized no-op passed |
| Project lifecycle | Unified Undo/Redo, modified state, immutable snapshot publication, save, and reload persistence passed |
| Public contract signature | Covered by the EtherCATCore compile-time contract test |
| Existing public virtual layout | New method appended after all existing Project service methods; prior method slots do not move |
| Focused EtherCATProject tests | 12 passed, 0 failed |
| Six-plugin isolated regression | Core 17, Project 12, Devices 8, Workbench 22, Scan 7, Diagnostics 7; 73 passed, 0 failed |
| Final isolated regression platform | Isolated HOME/settings and `QT_QPA_PLATFORM=offscreen`, avoiding focus-dependent Cocoa ActionManager context during headless runs |
| 16-plugin product build | Passed with the `WITH_TESTS=OFF` allow-list |
| Product version inventory | All 16 allow-listed plugins present and recognized at version 20.0.1 |
| Enabled product startup | All 16 plugins loaded, initialized, extended, and delayed-initialized; stable for 10 seconds until intentional interrupt |
| `EtherCATProject` disabled startup | Project, Workbench, Scan, and Diagnostics were dependency-disabled; remaining product stable for 10 seconds until intentional interrupt |
| User-visible UI | None by design in this Core/API prerequisite issue |
| CMake/qbs source lists | No source-list or dependency change required |
| Direct upstream Core, ProjectExplorer, or app changes | None; direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Blocked by the existing EasyBoard `extensionmanager_test.h` include defect |
| qbs build | Not run; qbs executable is unavailable |

## EtherCATWorkbench ESI Repository qualification

`ISSUE-WB-ESI-REPOSITORY-001` consumes the existing public
`DeviceRepositoryProvider`; Devices retains ownership of XML parsing, indexed
data, asynchronous jobs, and cancellation. Workbench adds only the dedicated
General-page presentation and guarded Provider/job references. No new public
contract, persistence, controller transport, or network behavior is added.

| Check | Result |
|---|---|
| Failure-first Workbench test | Compiled and failed because `EtherCATEsiRepositoryContent` did not exist; 2 passed and 1 failed as expected |
| TwinCAT-aligned repository workflow | `Import ESI Files...` and `Reload Device Descriptions` hierarchy checked against Beckhoff's official ESI and reload documentation; no Beckhoff assets or formats copied |
| Truthful repository summary | Status, indexed/supported/limited device counts, vendors, and distinct referenced source paths verified from the public Provider without fabricating stored-file or online state |
| Single/batch import entry | Multi-file XML picker boundary and local XML drag/drop implemented; non-local and non-XML drops are rejected |
| Partial-success import | One unique valid ESI and one malformed XML produced 2 requested, 1 imported, 1 failed, the parser error filename, updated summary, and a new device-tree entry |
| Reload existing descriptions | Provider rebuild completed, retained the imported device, refreshed counts, and reported zero failed files |
| Progress and cancellation | Deterministic controlled job verified 1/4 progress, enabled Cancel, one cancel call, synchronous final `Reload canceled` state, and disabled Cancel afterward |
| Error/result boundary | Requested/imported/updated/duplicate/failed/canceled counts are read-only; detailed errors are bounded to 100 entries |
| Focused repository flow | 3 passed, 0 failed |
| Focused repository flow at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Direct widget renders | Normal 1100 x 760 and 2x 2200 x 1520 renders passed visual inspection with no overlap, clipping, or scale drift |
| Focused EtherCATWorkbench suite | 27 passed, 0 failed |
| Six-plugin isolated regression | Core 17, Project 12, Devices 8, Workbench 27, Scan 7, Diagnostics 7; 78 passed, 0 failed |
| Final isolated regression platform | Separate processes with isolated HOME/settings and `QT_QPA_PLATFORM=offscreen`; high-DPI flow additionally used `QT_SCALE_FACTOR=2` |
| 16-plugin product build | Passed with the `WITH_TESTS=OFF` allow-list |
| Enabled product startup | Workbench loaded, initialized, extended, and delayed-initialized; product remained stable until the intentional 10-second timeout |
| Workbench-disabled startup | Workbench, Scan, and Diagnostics were absent as expected; Core, Devices, and Project initialized and the remaining product stayed stable until the intentional 10-second timeout |
| CMake/qbs source lists | Both list `esirepositorypage.cpp` and `esirepositorypage.h`; no dependency or plugin-metadata change |
| Direct upstream Core, ProjectExplorer, or app changes | None; direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Still blocked by the existing EasyBoard `extensionmanager_test.h` include defect; outstanding Ninja jobs were interrupted after the blocker was captured |
| qbs build | Not run; qbs executable is unavailable |
| Manual desktop interaction | Not run; only automated widget behavior and direct offscreen renders are claimed |

## EtherCATWorkbench ESI catalogue-device General qualification

`ISSUE-WB-ESI-DEVICE-GENERAL-001` consumes the existing immutable public
`DeviceDescription` snapshot. Devices retains ownership of ESI parsing,
preserved source data, identity, and qualification values; Workbench adds only
the private read-only presentation. No public contract, persistence field,
controller transport, or network behavior is added.

| Check | Result |
|---|---|
| Failure-first Workbench test | Compiled and failed because `EtherCATEsiDeviceGeneralContent` did not exist; 2 passed and 1 failed as expected |
| TwinCAT-aligned identity hierarchy | Name, Type, stable Object Id, Vendor ID, Product Code, Revision, and Group checked against Beckhoff's documented slave General hierarchy and adapted truthfully for a repository entry |
| Offline configuration coverage | SyncManager count, RxPDO/TxPDO summaries, CoE flags, Startup count, and DC-mode count verified from a real imported ESI description |
| Import qualification and source | Supported/Limited state, complete warning and unsupported-feature details, source path, SHA-256, and import time verified |
| Missing-description boundary | A removed or unresolved ESI device clears stale fields and displays an explicit unavailable state |
| Read-only and accessibility boundary | All value fields are read-only/selectable, long details wrap, groups and key controls have accessible names, and the generic property tree remains hidden and empty |
| Focused device General flow | 3 passed, 0 failed |
| Focused device General flow at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Direct widget renders | Normal top/bottom 1100 x 760 and 2x top/bottom 2200 x 1520 renders passed visual inspection with complete long text and no overlap, clipping, truncation, or scale drift |
| Focused EtherCATWorkbench suite | 28 passed, 0 failed |
| Six-plugin isolated regression | Core 17, Project 12, Devices 8, Workbench 28, Scan 7, Diagnostics 7; 79 passed, 0 failed |
| Final isolated regression platform | Separate processes with isolated HOME/settings and `QT_QPA_PLATFORM=offscreen`; high-DPI flow additionally used `QT_SCALE_FACTOR=2` |
| 16-plugin product build | Passed with the `WITH_TESTS=OFF` allow-list |
| Enabled product startup | Workbench initialized and delayed-initialized with clean temporary settings; product remained stable until the intentional 10-second interrupt |
| Workbench-disabled startup | Workbench, Scan, and Diagnostics were absent; Core, Devices, and Project initialized and the remaining product stayed stable until the intentional 10-second interrupt |
| CMake/qbs source lists | Both list `esidevicegeneralpage.cpp` and `esidevicegeneralpage.h`; no dependency or plugin-metadata change |
| Direct upstream Core, ProjectExplorer, or app changes | None; direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Still blocked by the existing EasyBoard `extensionmanager_test.h` include defect; outstanding Ninja jobs were interrupted after the blocker was captured |
| qbs build | Not run; qbs executable is unavailable |
| Manual desktop interaction | Not run; only automated widget behavior and direct offscreen renders are claimed |

## EtherCATWorkbench ESI insertion qualification

`ISSUE-WB-INSERT-DEVICE-001` consumes the existing immutable public
`DeviceSummary` catalogue and the checked Project replacement command.
Workbench adds one private selection dialog and a context-only Master command;
it adds no public API, persistence field, controller transport, network
behavior, or real EtherCAT operation.

| Check | Result |
|---|---|
| Failure-first Workbench test | Compiled and failed only because `EtherCAT.Workbench.InsertDevice` was not registered; 2 passed and 1 failed as expected |
| Official TwinCAT workflow reference | Master-side `Add New Item...`, ESI-backed selection, search, extended identity, and revision handling checked against Beckhoff's official Add New Item and EtherCAT revision documentation; no Beckhoff assets or formats copied |
| Action identity and context | One ActionManager command is present only in the real selected offline Master popup, is excluded from the compact strip, and disables after project close |
| Device selection behavior | Case-insensitive search, Extended Information, highest revision by default, Show Previous Revisions, explicit Supported/Limited state, and empty repository state passed |
| Checked mutation boundary | Explicit stable device/master IDs, current-revision append, ESI defaults, stable selection, Undo/Redo, cancel no-mutation, unsupported-device rejection, and stale-master rejection passed |
| Focused insertion flow | 3 passed, 0 failed |
| Focused insertion flow at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Direct dialog renders | Normal 1000 x 640 and 2x 2000 x 1280 renders passed visual inspection with all controls, identity columns, qualification state, and actions visible without overlap, clipping, or scale drift |
| Focused EtherCATWorkbench suite | 29 passed, 0 failed |
| Six-plugin isolated regression | Core 17, Project 12, Devices 8, Workbench 29, Scan 7, Diagnostics 7; 80 passed, 0 failed |
| Final isolated regression platform | Separate processes with isolated HOME/settings and `QT_QPA_PLATFORM=offscreen`; high-DPI flow additionally used `QT_SCALE_FACTOR=2` |
| 16-plugin product build | Passed with the `WITH_TESTS=OFF` allow-list |
| Enabled product startup | Workbench initialized and delayed-initialized with clean temporary settings; product remained stable until the intentional 10-second interrupt |
| Workbench-disabled startup | Workbench, Scan, and Diagnostics were absent; Core, Devices, and Project initialized and the remaining product stayed stable until the intentional 10-second interrupt |
| CMake/qbs source lists | Both list `esideviceselectiondialog.cpp` and `esideviceselectiondialog.h`; no dependency or plugin-metadata change |
| Direct upstream Core, ProjectExplorer, or app changes | None; direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Still blocked by the existing EasyBoard `extensionmanager_test.h` include defect; outstanding Ninja jobs were interrupted after the blocker was captured |
| qbs build | Not run; qbs executable is unavailable |
| Manual desktop interaction | Not run; only automated widget behavior and direct offscreen renders are claimed |

## EtherCATWorkbench ESI drag-and-drop qualification

`ISSUE-WB-DEVICE-DND-001` changes only the existing Workbench navigation,
tree model, controller, and tests. Beckhoff's official EtherCAT offline
documentation remains the reference for the primary tree-command and ESI
selection workflow; this goal-required Qt Creator drag-and-drop path is a local
convenience that calls the same checked insertion operation. It adds no public
API, persistence field, controller transport, network behavior, real EtherCAT
operation, or Beckhoff asset/format.

| Check | Result |
|---|---|
| Failure-first Workbench test | Compiled and failed only because the navigation tree had drag disabled; 2 passed and 1 failed as expected |
| View and proxy integration | Real `WorkbenchNavigationWidget` passed for DragDrop mode, viewport drop acceptance, drop indicator, Copy default, translated accessible guidance, and proxy-model drag/drop actions |
| Stable MIME contract | One Workbench-private MIME type contains only the bounded stable ESI device `NodeId`; no pointer or model index is serialized |
| Qualification and target gating | Only Supported catalogue devices are draggable and only the exact active valid offline Master is droppable; Limited, unknown, forged, slave, stale/wrong-target, MoveAction, and between-row paths are rejected |
| Checked mutation lifecycle | Accepted copy-drop leaves the repository unchanged, appends complete ESI identity/defaults at the next physical position, selects the new stable ID, and passes Project modified state, Undo, and Redo |
| Focused drag/drop flow | 3 passed, 0 failed |
| Focused drag/drop flow at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Focused EtherCATWorkbench suite | 30 passed, 0 failed |
| Six-plugin isolated regression | Core 17, Project 12, Devices 8, Workbench 30, Scan 7, Diagnostics 7; 81 passed, 0 failed |
| Final isolated regression platform | Separate processes with isolated HOME/settings and `QT_QPA_PLATFORM=offscreen`; high-DPI flow additionally used `QT_SCALE_FACTOR=2` |
| 16-plugin product build | Passed with the `WITH_TESTS=OFF` allow-list |
| Enabled product startup | Workbench initialized and delayed-initialized with clean temporary settings and no fatal/error signature before intentional termination |
| Workbench-disabled startup | Workbench, Scan, and Diagnostics were absent; Core, Devices, and Project initialized and delayed-initialized without fatal/error signatures before intentional termination |
| Lifecycle ownership | Controller-owned drop handler is cleared before the model; no timer, Provider, worker, background job, or cross-plugin object is retained |
| CMake/qbs source lists | Unchanged because the implementation adds no source file; no dependency or plugin-metadata change |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Still blocked by the existing EasyBoard `extensionmanager_test.h` include defect; outstanding Ninja jobs were intentionally interrupted after the blocker was captured |
| qbs build | Not run; qbs executable is unavailable |
| Visual/manual desktop inspection | No new geometry was introduced; widget behavior and normal/2x focused tests are claimed, but no new manual desktop drag is claimed |

## EtherCATWorkbench project General qualification

`ISSUE-WB-PROJECT-GENERAL-001` consumes the existing checked Project-name
command through the existing Workbench controller and property-page provider.
It adds no cross-plugin contract, persistent field, runtime Provider, or
transport.

| Check | Result |
|---|---|
| Failure-first Workbench test | Compiled and failed because `EtherCATProjectGeneralContent` did not exist; 2 passed and 1 failed as expected |
| TwinCAT-aligned project hierarchy | Editable Project name followed by stable Project ID and explicit offline project type verified against the documented Project tab |
| Truthful offline summary | Actual format version, creator, validation, migration, modified state, target/master names, and configured-slave count verified without fabricated runtime data |
| Checked editable boundary | Project name only; routed through `ProjectService::renameProject()` with normalization, empty rejection, modified state, persistence, Undo, and Redo |
| PLC/ADS and path boundary | AMS Port, boot encryption, autostart, symbolic mapping, multi-instance settings, and compiler definitions are absent; project path is omitted because the public contract does not expose it |
| Selection and presentation | Stable Project ID and selection, Workbench tree, ProjectExplorer display name, details title, and form remain synchronized across rename, Undo, and Redo |
| Stale-context behavior | Missing project becomes read-only and reports `Unavailable` instead of retaining or inventing values |
| Focused project General test | 3 passed, 0 failed |
| Combined Project/Target/Master/slave General regression | 6 passed, 0 failed |
| Focused project General at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Direct widget render | Normal 1100 x 720 and 2x 2200 x 1440 renders passed visual inspection with no overlap, clipping, or scale drift |
| Focused EtherCATWorkbench suite | 26 passed, 0 failed |
| Six-plugin isolated regression | Core 17, Project 12, Devices 8, Workbench 26, Scan 7, Diagnostics 7; 77 passed, 0 failed |
| Final isolated regression platform | Isolated HOME/settings and `QT_QPA_PLATFORM=offscreen`; high-DPI flow additionally used `QT_SCALE_FACTOR=2` |
| 16-plugin product build | Passed with the `WITH_TESTS=OFF` allow-list |
| Enabled product startup | All 16 plugins loaded, initialized, extended, and delayed-initialized; stable for 10 seconds until intentional timeout |
| Workbench-disabled startup | Workbench, Scan, and Diagnostics were dependency-disabled; remaining product was stable for 10 seconds until intentional timeout |
| CMake/qbs source lists | No source-list or dependency change required |
| Direct upstream Core, ProjectExplorer, or app changes | None; direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Still blocked by the existing EasyBoard `extensionmanager_test.h` include defect; outstanding Ninja jobs were interrupted after the blocker was captured |
| qbs build | Not run; qbs executable is unavailable |

## EtherCATWorkbench target General qualification

`ISSUE-WB-TARGET-GENERAL-001` consumes the qualified structural-name command
through the existing Workbench controller and property-page provider. It adds
no cross-plugin contract, persistent field, target Provider, or transport.

| Check | Result |
|---|---|
| Failure-first Workbench test | Compiled and failed because `EtherCATTargetGeneralContent` did not exist; 2 passed and 1 failed as expected |
| TwinCAT-aligned target hierarchy | Standard target summary and Choose Target action followed by Engineering, Target, Local, Project, and Pin Version verified against the documented target General page |
| Checked editable boundary | Target name only; routed through `ProjectService::renameStructuralNode()` with normalization, empty rejection, modified state, persistence, Undo, and Redo |
| Version truthfulness | Engineering uses the running product version; Project uses persisted format/creator values; Target is unassigned offline and Local is unavailable in phase 1 |
| Unsupported controls | Choose Target and Pin Version remain visible but unavailable with tooltip and accessibility explanations; no target discovery, connection, or runtime pin is simulated |
| Selection and presentation | Stable target ID and selection, tree label, details title, and form remain synchronized across rename, Undo, and Redo |
| Focused target General test | 3 passed, 0 failed |
| Combined Target/Master/slave General regression | 5 passed, 0 failed |
| Focused target General at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Direct widget render | Normal and 2x renders at the 1100 x 720 logical test size passed visual inspection with no overlap or clipping |
| Focused EtherCATWorkbench suite | 24 passed, 0 failed |
| Six-plugin isolated regression | Core 17, Project 12, Devices 8, Workbench 24, Scan 7, Diagnostics 7; 75 passed, 0 failed |
| Same-process optional-plugin experiment | Not a qualification gate: forcing all six test suites into one process produced four expected isolation conflicts because Core/Workbench tests deliberately exercise optional-provider absence; isolated plugin processes are authoritative |
| Final isolated regression platform | Isolated HOME/settings and `QT_QPA_PLATFORM=offscreen`; high-DPI flow additionally used `QT_SCALE_FACTOR=2` |
| 16-plugin product build | Passed with the `WITH_TESTS=OFF` allow-list |
| Product version inventory | All 16 allow-listed plugins loaded; running product reports version 20.0.1 |
| Enabled product startup | All 16 plugins loaded, initialized, extended, and delayed-initialized; stable for 10 seconds until intentional interrupt |
| Workbench-disabled startup | Workbench, Scan, and Diagnostics were dependency-disabled; remaining 13 plugins were stable for 10 seconds until intentional interrupt |
| CMake/qbs source lists | No source-list or dependency change required |
| Direct upstream Core, ProjectExplorer, or app changes | None; direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Still blocked by the existing EasyBoard `extensionmanager_test.h` include defect; outstanding parallel jobs were interrupted after the blocker was captured |
| qbs build | Not run; qbs executable is unavailable |

## EtherCATWorkbench master General qualification

`ISSUE-WB-MASTER-GENERAL-001` consumes the previously qualified structural-name
command through the existing Workbench controller and property-page provider.
It adds no new cross-plugin contract or persistent field.

| Check | Result |
|---|---|
| Failure-first Workbench test | Compiled and failed because `EtherCATMasterGeneralForm` did not exist; 2 passed and 1 failed as expected |
| TwinCAT-aligned master hierarchy | Name and Id first row, Object Id, Type, Comment, Disabled, and Create symbols verified against the documented master General page |
| Checked editable boundary | Name only; routed through `ProjectService::renameStructuralNode()` with normalization, empty rejection, modified state, persistence, Undo, and Redo |
| Unsupported settings | Comment, Disabled, and Create symbols remain visible but unavailable because the phase-1 project contract does not persist them |
| Offline summary | Explicit unassigned cycle placeholder, actual configured-slave count, and shared Workbench status verified without fabricated online data |
| Selection and presentation | Stable master ID and selection, tree label, details title, and form remain synchronized across rename, Undo, and Redo |
| Focused master General test | 3 passed, 0 failed |
| Combined master/slave General regression | 4 passed, 0 failed |
| Focused master General at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Direct widget render | Normal and 2x renders at the 1100 x 760 logical test size passed visual inspection with no overlap, clipping, or uncontrolled expansion |
| Focused EtherCATWorkbench suite | 23 passed, 0 failed |
| Six-plugin isolated regression | Core 17, Project 12, Devices 8, Workbench 23, Scan 7, Diagnostics 7; 74 passed, 0 failed |
| Final isolated regression platform | Isolated HOME/settings and `QT_QPA_PLATFORM=offscreen`; high-DPI flow additionally used `QT_SCALE_FACTOR=2` |
| 16-plugin product build | Passed with the `WITH_TESTS=OFF` allow-list |
| Product version inventory | All 16 allow-listed plugins present and recognized at version 20.0.1 |
| Enabled product startup | All 16 plugins loaded, initialized, extended, and delayed-initialized; stable for 10 seconds until intentional interrupt |
| Workbench-disabled startup | Workbench, Scan, and Diagnostics were dependency-disabled; remaining 13 plugins were stable for 10 seconds until intentional interrupt |
| CMake/qbs source lists | No source-list or dependency change required |
| Direct upstream Core, ProjectExplorer, or app changes | None; direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Still blocked by the existing EasyBoard `extensionmanager_test.h` include defect |
| qbs build | Not run; qbs executable is unavailable |

## EtherCATWorkbench master EtherCAT qualification

`ISSUE-WB-MASTER-ETHERCAT-001` changes only the existing Workbench property
page and its tests. It consumes the immutable offline Project snapshot and adds
no cross-plugin contract, persistent field, controller Provider, ADS behavior,
runtime task, or transport.

| Check | Result |
|---|---|
| Failure-first Workbench test | Compiled and failed because `EtherCATMasterEthercatForm` did not exist; 2 passed and 1 failed as expected |
| TwinCAT-aligned master EtherCAT hierarchy | Read-only NetId followed by Advanced Settings, Export Configuration File, Sync Unit Assignment, and Topology actions, plus the ten documented cyclic-frame columns |
| Truthful unsupported boundary | NetId says `Not assigned (offline)`; Advanced, Export, and Sync Unit actions remain disabled with tooltip/accessibility explanations; no ADS route, export format, runtime task, or Sync Unit state is simulated |
| Offline topology action | Passed for Position, Name, derived Auto Inc Addr, predecessor, explicit unmodeled Port, Vendor/Product/Revision, Alias, and `Offline configured` status |
| Current-snapshot and empty behavior | Each dialog invocation rereads the master snapshot; removing all slaves produces zero rows and an explicit `No configured slaves` summary |
| Cyclic-frame boundary | All ten headers remain visible with zero rows and an explicit explanation that no runtime frame can be generated |
| Focused master EtherCAT test | 3 passed, 0 failed |
| Master/slave EtherCAT and master General regression | 6 passed, 0 failed |
| Focused master EtherCAT at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Direct widget renders | Normal and 2x master-page and topology-dialog renders passed visual inspection with all controls and columns readable and no overlap or clipping |
| Focused EtherCATWorkbench suite | 25 passed, 0 failed |
| Six-plugin isolated regression | Core 17, Project 12, Devices 8, Workbench 25, Scan 7, Diagnostics 7; 76 passed, 0 failed |
| Final isolated regression platform | Isolated HOME/settings and `QT_QPA_PLATFORM=offscreen`; high-DPI flow additionally used `QT_SCALE_FACTOR=2` |
| 16-plugin product build | Passed with the `WITH_TESTS=OFF` allow-list |
| Enabled product startup | All 16 plugins loaded, initialized, extended, and delayed-initialized; stable for 10 seconds until intentional timeout |
| Workbench-disabled startup | Workbench, Scan, and Diagnostics were dependency-disabled; remaining 13 plugins were stable for 10 seconds until intentional timeout |
| CMake/qbs source lists | No source-list or dependency change required |
| Direct upstream Core, ProjectExplorer, or app changes | None; direct Core patch count remains five |
| Full product build with `WITH_TESTS=ON` | Still blocked by the existing EasyBoard `extensionmanager_test.h` include defect; outstanding parallel jobs were interrupted after the blocker was captured |
| qbs build | Not run; qbs executable is unavailable |

## EtherCATProject current qualification

The public Project contract uses immutable `EtherCATData` snapshots and
`ProjectExplorer::ProjectManager` lifecycle signals. ProjectExplorer objects,
documents, models, and indexes never cross the plugin boundary.

| Check | Result |
|---|---|
| Focused Qt Creator plugin tests | 12 passed, 0 failed |
| Format round trip and corruption | Passed |
| Version-0 and version-1 migration with exact backups | Passed |
| Version-2 Process Data, Startup, and DC persistence | Passed |
| Missing configuration, duplicate stable ID, invalid raw hex, and invalid mapping rejection | Passed |
| Undo/Redo and Save All modified state | Passed |
| Atomic save failure preserves source | Passed |
| Two-project open/switch/close lifecycle | Passed |
| Target/Master rename validation, persistence, service command, and Undo/Redo | Passed |
| Offline slave validation, persistence, service command, and Undo/Redo | Passed |
| Process Data, Startup, and DC service commands and Undo/Redo | Passed |
| Scan acceptance preserves matched offline configuration | Passed |
| EtherCAT plugin regressions | Core 17, Project 12, Devices 8, Workbench 22, Scan 7, Diagnostics 7 passed |
| Normal Release product build | Passed with 16-plugin allow-list |
| Product version inventory | All 16 allow-listed plugins present and recognized |
| Clean-settings GUI startup | Passed; stable for 5 seconds until intentional `SIGTERM`; empty log |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Full product build with `WITH_TESTS=ON` | Blocked by existing EasyBoard test include defect |
| qbs build | Not run; qbs executable is unavailable |

## EtherCATDevices stage-3 qualification

The Devices API uses immutable `EtherCATData` values and provider-owned
cancellable jobs. It contains no XML parser object, item-model index, network
transport, or controller type. The repository implementation is documented in
`docs/ethercat-devices-repository.md`.

| Check | Result |
|---|---|
| Focused EtherCATDevices plugin tests | 8 passed, 0 failed |
| Malformed XML and mandatory identity rejection | Passed |
| Multiple product revisions and deterministic IDs | Passed |
| SM, RxPDO/TxPDO, CoE startup, DC, and common types | Passed |
| Exact source XML, SHA-256 integrity, duplicate import | Passed |
| Search/vendor filters and asynchronous persisted rebuild | Passed |
| 300-device ESI library | Passed |
| Pending-job cancellation and deferred cleanup | Passed |
| EtherCATCore regression tests | 12 passed, 0 failed |
| EtherCATProject regression tests | 8 passed, 0 failed |
| Normal Release product build | Passed with 13-plugin allow-list |
| Enabled and disabled GUI startup | Passed; stable until intentional interrupt |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Full product build with `WITH_TESTS=ON` | Blocked by existing EasyBoard test include defect |
| qbs build | Not run; qbs executable is unavailable |

The value-object, import-job, and property-page contracts remain covered by
the focused EtherCATCore suite.

## EtherCATWorkbench stage-4 qualification

The Workbench uses Qt Creator's public mode, navigation, output-pane, context,
and ActionManager APIs. It consumes public Project and Devices services and
uses stable IDs across the selection boundary. Its implementation and current
limits are documented in `docs/ethercat-workbench.md`.

| Check | Result |
|---|---|
| Focused EtherCATWorkbench plugin tests | 39 passed, 0 failed on the offscreen qualification path |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 39, Scan 7, Diagnostics 7; 90 passed, 0 failed in isolated offscreen processes |
| Failure-first tree contract test | Failed to compile on missing source-ID routing before implementation, as expected |
| Failure-first navigation layout test | Failed on `ElideRight`, then on missing accessible metadata, before both fixes |
| Failure-first navigation keyboard test | Compiled and failed because the navigation container focus proxy was null, as expected |
| Focused navigation keyboard test | 3 passed, 0 failed at normal scale |
| Focused navigation keyboard test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Failure-first navigation filter empty-state test | Compiled and failed because the explicit no-match widget did not exist; 2 passed and 1 failed as expected |
| Focused navigation filter empty-state test | 3 passed, 0 failed at normal scale |
| Focused navigation filter empty-state test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Failure-first active-project navigation test | Compiled and failed on actual `Offline` versus expected `Active project \| Offline`; 2 passed and 1 failed as expected |
| Focused active-project lifecycle test | 3 passed, 0 failed at normal scale |
| Focused active-project lifecycle test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Active-project navigation direct Qt renders | Passed at normal 720 x 360 and 2x 1440 x 720 with both project roots, one exact textual active marker, and no clipping, overlap, or scale drift |
| Failure-first set-active-project command test | Compiled and failed only because `EtherCAT.Workbench.SetActiveProject` was not registered; 2 passed and 1 failed as expected |
| Focused set-active-project command test | 3 passed, 0 failed at normal scale |
| Focused set-active-project command test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Set-active-project direct Qt menu renders | Passed at 504 x 376 and 1008 x 752 with separate project/copy groups and no clipping, overlap, or scale drift |
| Navigation context proxy handoff | Passed by triggering the popup's registered command proxy in both Workbench and Edit-mode navigation contexts |
| Set-active-project desktop interaction inspection | Not run; only direct menu renders and offscreen widget behavior are claimed |
| Failure-first ESI repository empty-guidance test | Compiled and failed because the placeholder still said to use an import command in a later UI stage; 2 passed and 1 failed as expected |
| Focused ESI repository empty-guidance test | 3 passed, 0 failed at normal scale |
| Focused ESI repository empty-guidance test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| ESI repository empty-guidance direct Qt renders | Passed at normal 720 x 360 and 2x 1440 x 720 output with the complete recovery guidance visible without clipping, overlap, or scale drift |
| Failure-first CoE Online page test | Compiled and failed on the missing `CoE Online` page descriptor before implementation, as expected |
| Failure-first unified-status test | Compiled and failed because no Workbench status-bar control was registered, as expected |
| Failure-first command-strip test | Compiled and failed because EtherCAT Mode had no engineering command strip, as expected |
| Failure-first provider-state tree test | Failed to compile on missing difference/issue/diagnostics navigation APIs before implementation, as expected |
| Failure-first optional-Provider presentation test | Compiled and failed on actual `Plugin not installed` versus the truthful absent/Mock contract; 2 passed and 1 failed as expected |
| Focused optional-Provider presentation test | 3 passed, 0 failed at normal scale |
| Focused optional-Provider presentation test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Optional-Provider direct Qt renders | Passed at 900 x 600 and 1800 x 1200 with the complete unavailable Mock explanation visible without clipping, overlap, or scale drift |
| Failure-first context-command test | Failed to compile only on missing Locate Unsupported/Copy Node ID command IDs and Controller requests before implementation, as expected |
| Failure-first offline-topology test | Failed to compile only on the four missing Add/Remove/Move ActionManager command IDs before implementation, as expected |
| Failure-first configured-slave General test | Compiled and failed because the editable `EtherCATGeneralName` control did not exist before implementation, as expected |
| Failure-first project General test | Compiled and failed because `EtherCATProjectGeneralContent` did not exist before implementation, as expected |
| Failure-first target General test | Compiled and failed because `EtherCATTargetGeneralContent` did not exist before implementation, as expected |
| Failure-first master General test | Compiled and failed because `EtherCATMasterGeneralForm` did not exist before implementation, as expected |
| Failure-first master EtherCAT test | Compiled and failed because `EtherCATMasterEthercatForm` did not exist before implementation, as expected |
| Failure-first configured-slave EtherCAT test | Compiled and failed because the dedicated `EtherCATEthercatAlias` control did not exist before implementation, as expected |
| Failure-first ESI repository test | Compiled and failed because `EtherCATEsiRepositoryContent` did not exist before implementation, as expected |
| Failure-first ESI catalogue-device General test | Compiled and failed because `EtherCATEsiDeviceGeneralContent` did not exist before implementation; 2 passed and 1 failed as expected |
| Failure-first ESI insertion test | Compiled and failed only because `EtherCAT.Workbench.InsertDevice` was not registered; 2 passed and 1 failed as expected |
| Failure-first ESI drag/drop test | Compiled and failed only because the navigation tree had drag disabled; 2 passed and 1 failed as expected |
| Metadata, hard dependencies, mode, and actions | Passed |
| Shared QAction identity across menu, toolbar, shortcuts, and callbacks | Passed |
| Dynamic action add/remove and optional Scan/Diagnostics load combinations | Passed with both, either, and neither optional plugin loaded |
| Compact command icons, complete tooltips, and standard toolbar metric | Passed |
| Focused command-strip test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Combined command-strip/provider-state test at `QT_SCALE_FACTOR=2` | 4 passed, 0 failed |
| Combined command-strip/tree-command test at `QT_SCALE_FACTOR=2` | 4 passed, 0 failed |
| Combined command-strip/offline-topology test at `QT_SCALE_FACTOR=2` | 4 passed, 0 failed |
| 500-device incremental model with model tester | Passed |
| Filter, context, and bidirectional stable selection | Passed |
| Navigation filter no-match and recovery | Passed for non-empty zero-row queries, Clear Filter by Space key, visible focus-proxy switching, dynamic proxy insertion/removal, long Unicode input, and exact stable-selection recovery |
| Navigation filter accessibility | Passed for translated filter name/description, no-match state metadata, and keyboard-reachable clear action |
| Multi-project active status | Passed for exact Display, Status, Search, and tooltip projection: one `Active project \| Offline` root while all other project roots remain `Offline` |
| Active switch stability | Passed without model reset or persistent-index invalidation; tree selection did not activate a project and stable selection plus Details context remained unchanged |
| Active filter and drop-target handoff | Passed for dynamic `Active project` filter rematch and exact active offline Master drop flags/tooltips |
| Real open/switch/close lifecycle | Passed for two `.ecatproject` files, active-project close fallback to the remaining project, stale-selection cleanup, and final no-project state |
| Inactive-project context command | Passed for exact ActionManager identity and text, tooltip/status boundary, project-only popup placement, active/non-Project/empty exclusion, shared-menu/command-strip exclusion, and real popup triggering from Workbench and Edit modes |
| Explicit active-project switch | Passed through the existing public Project service with stable Selection/Details, persistent indexes, no model reset, unchanged Project snapshots, filter/drop-target handoff, and close fallback |
| Process Data, Startup, and DC pages from imported ESI | Passed |
| TwinCAT-inspired SM, PDO Assignment, PDO List, PDO Content layout | Passed in widget/model flow test |
| RxPDO/TxPDO selection and synchronized PDO content | Passed |
| Read-only ESI/fixed mapping and mandatory assignment constraints | Passed |
| ESI-derived initial mapping and no-ESI empty state | Passed |
| Checked assignment, bit-offset, and data-type editing | Passed |
| Invalid width rejection and process-image refresh | Passed |
| Real DetailsView, ProjectService, modified state, Undo, and Redo | Passed |
| TwinCAT-inspired CoE object hierarchy and five-column layout | Passed under `QAbstractItemModelTester` |
| CoE manual refresh, search, Unicode, and advanced range filters | Passed |
| CoE Mock raw-value edit and invalid-width rejection | Passed |
| CoE Add to Startup cancel, confirm, append-only, and Undo | Passed |
| CoE offline, missing-ESI, and repository read-only boundaries | Passed; repository `RW` capability metadata remains visible while its Mock Value edit flag and direct model mutation are rejected by the separate current qualification below |
| CoE focused test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Shared StateService Offline/Busy/Error priority and Mock labels | Passed |
| Status tooltip, drop-down details, Mode visibility, and cleanup ownership | Passed |
| Status icon style metric and content-width guard | Passed |
| Focused status test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed after fixing the reproduced width compression |
| TwinCAT-inspired ordered Startup list and action layout | Passed in widget/model flow test |
| Startup ESI proposal, explicit Store/Restore, and read-only catalogue | Passed |
| Startup New/Edit/Delete, enable, Move Up/Down, and fixed request constraints | Passed |
| Startup type/raw-value/order rejection and Undo/Redo | Passed |
| Startup manual no-ESI empty state | Passed |
| TwinCAT-inspired DC Cyclic Mode, SYNC0, SYNC1, and reference-clock layout | Passed in widget/model flow test |
| Two ESI DC modes, explicit Store/Restore, and read-only catalogue | Passed |
| Manual no-ESI mode, AssignActivate, cycle/shift, and dependency validation | Passed |
| DC field, mode, defaults, dependent disable, and reference-clock Undo/Redo | Passed |
| Configured-slave tree, empty-placeholder removal, and ESI page reuse | Passed |
| ESI repository summary and actions | Passed for truthful indexed/supported/limited/vendor/referenced-source counts, multi-file import, XML drag/drop, reload, progress, cancel, and bounded results |
| ESI import partial failure | Passed with one valid and one malformed XML: successful device retained, parser failure named, tree refreshed, and no all-or-nothing claim |
| ESI catalogue-device General identity and coverage | Passed for imported bilingual Name, Type, stable Object Id, identity keys, Group, SyncManagers, PDOs, CoE flags, Startup values, and DC modes |
| ESI catalogue-device qualification and source | Passed for Limited status, complete warning/unsupported details, source path, SHA-256, import time, read-only accessibility, scrolling, and missing-description state |
| Master-side ESI selection dialog | Passed for Master-only registered action, search, extended identity, highest-revision default, previous revisions, empty state, and supported/limited qualification gating |
| Master-side ESI insertion lifecycle | Passed for explicit stable master/device IDs, append defaults, stable selection, Project Undo/Redo, cancel no-mutation, unsupported-device rejection, stale-master rejection, and command disable after close |
| ESI device drag/drop lifecycle | Passed for real navigation/proxy configuration, private stable-ID CopyAction, Supported-only source, exact active-Master target, repository-preserving append, stable selection, and Project Undo/Redo |
| ESI drag/drop rejection boundary | Passed for Limited/unknown/forged device IDs, MoveAction, slave/wrong targets, and between-row drops |
| Supported ESI device add and repeated-device unique naming | Passed with complete identity, repository reference, Process Data, Startup, and DC defaults |
| Offline slave remove/reorder workflow | Passed with explicit consequence confirmation, default/Escape No, context-drift rejection, normalized positions, stable selection, selection repair, complete Undo restoration, and Redo |
| Offline-topology ActionManager identity | Passed for all four context-only commands in real device and configured-slave popup menus |
| Configured-slave General identity form | Passed for editable Name and read-only one-based Id, stable Object Id, and ESI-derived Type |
| Configured-slave General rename workflow | Passed for whitespace trimming, Unicode, empty rejection, Project modified state, tree/title/form synchronization, stable selection, Undo, and Redo |
| Configured-slave General missing ESI state | Passed with explicit `Unknown ESI device` Type and retained read-only identity details |
| Focused configured-slave General test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Project General identity form | Passed for editable Project name, stable Project ID, explicit offline project type, and the actual format/creator/validity/migration/modified/topology summary |
| Project General rename workflow | Passed for whitespace trimming, Unicode, empty rejection, Project modified state, Workbench tree/ProjectExplorer/title/form synchronization, stable selection, Undo, and Redo |
| Project General stale context | Passed with a read-only name and explicit `Unavailable` validity instead of stale or fabricated values |
| Focused project General test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed with a visually inspected 2x render |
| Master General identity form | Passed for TwinCAT-aligned Name/Id, stable Object Id, Type, visible unsupported settings, and offline summary |
| Master General rename workflow | Passed for whitespace trimming, Unicode, empty rejection, Project modified state, tree/title/form synchronization, stable selection, Undo, and Redo |
| Focused master General test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed with a visually inspected 2x render |
| Master EtherCAT NetId/action/frame hierarchy | Passed with read-only offline NetId, four documented actions, and all ten cyclic-frame columns |
| Master EtherCAT unsupported runtime boundary | Passed with disabled Advanced/Export/Sync Unit actions, zero generated frame rows, and complete tooltip/accessibility explanations |
| Master EtherCAT offline topology | Passed for current-snapshot population, complete identity/order fields, explicit unmodeled port, configured status, and empty topology after slave removal |
| Focused master EtherCAT test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed with visually inspected master-page and topology-dialog renders |
| Configured-slave EtherCAT field hierarchy | Passed for Type, Product/Revision, Auto Inc Addr, explicit fixed-address state, Alias, Identification Value, Previous Port, and Advanced Settings |
| Auto-increment address and predecessor derivation | Passed for first `0x0000`, second `0xffff`, master predecessor, and previous-slave predecessor without inventing a port number |
| Configured Station Alias workflow | Passed for 0 through 65535 bounds, explicit zero-disable state, Project modified state, stable selection, Undo, and Redo |
| EtherCAT page read-only and missing-data boundaries | Passed for repository ESI view, dedicated master topology action/frame schema, retained SyncManager table, missing ESI type, and empty missing-ESI table |
| Focused configured-slave EtherCAT test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Exact Inputs, Outputs, RxPDO, TxPDO, Modules/Channels branch order | Passed |
| Inputs from active TxPDO and Outputs from active RxPDO | Passed |
| Active-PDO projection with PDO and Entry descendants | Passed |
| Unique stable view IDs and retained source-domain IDs | Passed |
| Recursive derived-node filtering and stable Selection Service linkage | Passed |
| Derived Process Data focus and read-only mutation boundary | Passed |
| Explicit unsupported Modules/Channels empty state | Passed |
| 128 configured slaves under `QAbstractItemModelTester` | Passed |
| Non-elided content-sized tree columns in a narrow navigation area | Passed in widget test and desktop inspection |
| Accessible tree name, description, and five process-data branches | Passed in widget test and macOS accessibility inspection |
| Navigation activation focus and arrow-key selection | Passed at normal scale and `QT_SCALE_FACTOR=2` with outer-widget focus transfer, a real Down-arrow event, and stable `NodeId` publication |
| Navigation filter direct Qt renders | Passed at normal 420 x 480 and 2x 840 x 960 output with the full long Unicode query, centred no-match message, and Clear Filter action visible without overlap, clipping, or scale drift |
| macOS accessibility lock-transition observation | One Qt 6.11 accessibility crash was captured during a lock transition; a second RxPDO-selection run remained alive until the Mac locked, so the event is not reproduced and remains a qualification risk |
| Dynamic property-page provider removal | Passed |
| Dynamic Scan/Diagnostics availability and removal | Passed |
| Optional Provider capability truth | Passed for absent, registered/unavailable, available, public display-name refresh without page reconstruction, scored multi-Provider source/name identity, pre-snapshot source neutrality, removal/re-addition, stale-overlay cleanup, no-page/fallback guidance, Search/ToolTip projection, stable selection, and accessibility text |
| Workbench test-process cleanup | Set-active-project focused normal/2x and the offscreen full-suite process exited 0 after real two-project open/switch/close, widget, controller, selection, and Provider cleanup; this issue adds no thread, timer, future, or Provider |
| Scan snapshot overlay | Passed for exact, Missing, Added, Revision, Vendor, source label, full-detail search, and aggregate count/severity |
| Diagnostics snapshot overlay | Passed for Run/OP, SAFEOP/error, AL detail, missing snapshot, alarm/error marker, stopped state, and cleanup |
| Difference/issue/Diagnostics ActionManager navigation | Passed with stable selection, filter clearing, ancestor expansion, and shared QAction registration |
| Tree context and navigation ActionManager identity | Passed for eight navigation and five offline-topology commands, shared Expand/Collapse buttons, context-only strip exclusion, enabled state, stable-ID copy, and placeholder protection |
| EtherCATCore regression tests | 17 passed, 0 failed |
| EtherCATProject regression tests | 12 passed, 0 failed |
| EtherCATDevices regression tests | 8 passed, 0 failed |
| EtherCATScan regression tests | 7 passed, 0 failed |
| EtherCATDiagnostics regression tests | 7 passed, 0 failed |
| Product version inventory | All 16 allow-listed plugins present and recognized |
| Normal `WITH_TESTS=OFF` product build | Passed with 16-plugin allow-list |
| Enabled offscreen GUI startup | Passed with clean temporary settings; remained stable beyond 10 seconds, then exited 143 after intentional SIGTERM |
| Explicitly disabled offscreen startup | Passed with `-noload EtherCATWorkbench` and clean temporary settings; remained stable beyond 10 seconds, then exited 143 after intentional SIGTERM; no product or test process remained |
| Visual desktop inspection | Passed with all five process-data branches, PDO/Entry descendants, explicit modular empty state, normal Creator icon scale, and readable narrow-sidebar names |
| CoE direct Qt Widget render | Passed at 2200 x 1520 Retina output with hierarchy, values, Mock banner, and bilingual long name visible without overlap |
| CoE Online desktop interaction inspection | Not run; macOS was locked, so no CoE screenshot or manual-click result is claimed |
| Unified-status direct Qt main-window render | Passed at 2520 x 1400; `MOCK Fault` and the standard-sized icon remained fully visible without overlap |
| Unified-status desktop interaction inspection | Not run; Computer Use reported a locked macOS session, so no manual-click result is claimed |
| Command-strip direct Qt main-window render | Passed at 2520 x 1400 with Workbench, Mock Scan, and Mock Diagnostics actions visible in one compact row without text compression or overlap |
| Command-strip manual interaction inspection | Not run; the current desktop was not manually clicked, so only direct render and QAction behavior tests are claimed |
| Provider-state tree direct Qt render | Passed at 2400 x 1600 Retina output with MOCK Run/OP, SAFEOP/Error, Missing, Revision, Added, difference count, and Diagnostics state visible without clipping |
| Tree context-menu direct Qt render | Passed at 522 x 472 Retina output with the original seven commands plus configured-slave Remove/Move commands, correct boundary state, and no clipping at `QT_SCALE_FACTOR=2`; offscreen macOS style omitted menu icons |
| Offline-topology desktop interaction inspection | Not run; Computer Use reported a locked macOS session, so no manual-click result is claimed |
| Configured-slave General direct Qt render | Passed at 2200 x 1440 Retina output with Unicode title/name, Id, Object Id, Type, and details visible without overlap or clipping |
| Configured-slave General desktop interaction inspection | Not run; only the direct offscreen render and widget behavior tests are claimed |
| ESI Device Repository direct Qt renders | Passed at normal 1100 x 760 and 2x 2200 x 1520 output with summary, actions, progress, partial-success result, and parser error visible without overlap, clipping, or scale drift |
| ESI Device Repository desktop interaction inspection | Not run; only direct offscreen renders and widget behavior tests are claimed |
| ESI catalogue-device General direct Qt renders | Passed for normal top/bottom 1100 x 760 and 2x top/bottom 2200 x 1520 output; complete long details remained visible without overlap, clipping, truncation, or scale drift |
| ESI catalogue-device General desktop interaction inspection | Not run; only direct offscreen renders and widget behavior tests are claimed |
| ESI device selection dialog direct Qt renders | Passed at normal 1000 x 640 and 2x 2000 x 1280 output with search, revision controls, identity columns, qualification state, and Add/Cancel actions visible without overlap, clipping, or scale drift |
| ESI device selection dialog desktop interaction inspection | Not run; only direct offscreen renders and widget behavior tests are claimed |
| Offline-project General direct Qt renders | Passed at normal 1100 x 720 and 2x 2200 x 1440 output with identity and complete offline summary visible without overlap, clipping, or scale drift |
| Offline-project General desktop interaction inspection | Not run; only the direct offscreen renders and widget behavior tests are claimed |
| Configured-slave EtherCAT direct Qt render | Passed at 2200 x 1520 Retina output with all supported and explicit unavailable fields plus two SyncManager rows visible without overlap or clipping |
| Configured-slave EtherCAT desktop interaction inspection | Not run; only the direct offscreen render and widget behavior tests are claimed |
| EtherCAT-master EtherCAT direct Qt renders | Passed at normal and 2x scale for the 1180 x 760 logical page and populated topology dialog; all ten frame and topology columns remained readable without overlap or clipping |
| EtherCAT-master EtherCAT desktop interaction inspection | Not run; only direct offscreen renders and widget behavior tests are claimed |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Full product build with `WITH_TESTS=ON` | Blocked by existing EasyBoard test include defect |
| qbs build | Not run; this issue changes no CMake/qbs target description; qbs 3.2.0 was found outside `PATH` |

## EtherCATScan stage-5 qualification

The Scan plugin is a local simulation. None of this evidence represents a
physical interface scan, controller connection, EtherCAT frame exchange, or
hardware result. Its behavior and phase limits are documented in
`docs/ethercat-scan.md`.

| Check | Result |
|---|---|
| Focused EtherCATScan plugin tests | 7 passed, 0 failed |
| Metadata, dependencies, Providers, commands, and page widgets | Passed |
| Exact, added, missing, reordered, identity, duplicate, PDO/DC placeholder differences | Passed |
| Normal, slow, interface-only, cancellation, partial failure, and shutdown paths | Passed |
| Revision mismatch and selected-branch comparison | Passed |
| Interface-scan acceptance protection | Passed |
| Stable-ID branch merge and independent Project Undo/Redo | Passed |
| EtherCATCore regression tests | 12 passed, 0 failed |
| EtherCATProject regression tests | 8 passed, 0 failed |
| EtherCATDevices regression tests | 8 passed, 0 failed |
| EtherCATWorkbench regression tests | 9 passed, 0 failed |
| Normal Release product build | Passed with 15-plugin allow-list |
| Enabled GUI startup | Passed; stable for 5 seconds until intentional interrupt |
| Explicitly disabled startup | Passed with `-noload EtherCATScan`; one shared-memory warning, then stable for 5 seconds until intentional interrupt |
| Visual desktop inspection | Blocked by locked Mac session; not claimed as passed |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Full product build with `WITH_TESTS=ON` | Blocked by existing EasyBoard test include defect |
| qbs build | Not run; qbs executable is unavailable |

## Diagnostics API pre-stage-6 qualification (historical gate)

This earlier issue froze only the public Diagnostics data and Provider
contract. At that gate, the Mock plugin, sampling implementation, and live UI
were still pending; their completed qualification is recorded below.

| Check | Result |
|---|---|
| EtherCATCore contract tests | 13 passed, 0 failed |
| EtherCATProject regression tests | 8 passed, 0 failed |
| EtherCATDevices regression tests | 8 passed, 0 failed |
| EtherCATWorkbench regression tests | 9 passed, 0 failed |
| EtherCATScan regression tests | 7 passed, 0 failed |
| Normal Release product targets | Application and current EtherCAT dependency set passed |
| Enabled clean-settings startup | Passed for 5 seconds; one shared-memory warning, then intentional `SIGTERM` |
| Explicitly disabled startup | Passed for 5 seconds with `-noload EtherCATCore` |
| Diagnostics values and commands | Snapshot, WKC, link, counters, DC, cycle, events, alarms, trends, mode, stop, and failure covered |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Full product build with `WITH_TESTS=ON` | Blocked by existing EasyBoard test include defect |
| qbs build | Not run; qbs executable is unavailable |

## EtherCATDiagnostics stage-6 qualification

The Diagnostics plugin is a local simulation. None of this evidence represents
controller communication, an EtherCAT frame, physical WKC, measured DC offset,
or a Zynq result. Its boundaries and implementation are documented in
`docs/ethercat-diagnostics.md`.

| Check | Result |
|---|---|
| Focused EtherCATDiagnostics plugin tests | 7 passed, 0 failed |
| Metadata, dependencies, Providers, commands, and pages | Passed |
| Stream state, Config/FreeRun/Run modes, WKC, DC, link, counter, and cycle fields | Passed |
| 10 ms source and 100 ms UI publication decoupling | Passed |
| Bounded event/trend histories and oldest-first drop counters | Passed |
| Active, Acknowledged, Recovered, repeat, and clear alarm lifecycle | Passed |
| Source failure, final-consumer, project-change, project-close, and shutdown cleanup | Passed |
| Live page tables, ActionManager commands, trend, and repeated publication | Passed |
| EtherCATCore regression tests | 13 passed, 0 failed |
| EtherCATProject regression tests | 8 passed, 0 failed |
| EtherCATDevices regression tests | 8 passed, 0 failed |
| EtherCATWorkbench regression tests | 9 passed, 0 failed |
| EtherCATScan regression tests | 7 passed, 0 failed |
| Normal Release product build | Passed with 16-plugin allow-list |
| Product version inventory | EtherCATDiagnostics 20.0.1 present |
| Enabled GUI startup | Passed; stable for 5 seconds until intentional `SIGTERM` |
| Explicitly disabled startup | Passed with `-noload EtherCATDiagnostics` |
| Core dependency disabled startup | Passed with `-noload EtherCATCore` |
| Visual desktop inspection | Passed with two slaves, eight pages, live WKC 4/4, counters, trend, Start, and Stop |
| Network or physical hardware access | Not performed by design; implementation contains no network API |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Full product build with `WITH_TESTS=ON` | Blocked by existing EasyBoard test include defect |
| qbs build | Not run; qbs executable is unavailable |

## EtherCATWorkbench Details empty-state qualification

`ISSUE-WB-DETAILS-EMPTY-LIFECYCLE-001` is presentation-only. It does not add a
controller connection, EtherCAT frame, physical-device result, network access,
or runtime state.

| Check | Result |
|---|---|
| Failure-first focused test | Failed as expected because the zero-project Details area still displayed the generic selection prompt |
| Zero-project guidance | Passed with project create/open direction and the existing local Device Repository recovery path |
| Open-project/no-selection guidance | Passed with an explicit offline node-selection prompt |
| Project lifecycle | Passed for real temporary project open, valid selection, selection clear, final close, and no stale Details page |
| Accessibility metadata | Passed for the Details container, dynamic title, guidance label, and property tabs |
| Focused normal-scale test | 3 passed, 0 failed |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed with both 1800 x 1200 empty-state renders inspected |
| Complete EtherCATWorkbench suite | 36 passed, 0 failed in an isolated offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 36, Scan 7, Diagnostics 7; 87 passed, 0 failed in isolated offscreen processes |
| Product version inventory | All 16 allow-listed plugins present |
| Normal `WITH_TESTS=OFF` product build | Passed |
| Enabled offscreen startup | Passed with clean temporary settings for 12 seconds, then exited 143 after intentional SIGTERM |
| Explicitly disabled offscreen startup | Passed with `-noload EtherCATWorkbench` and clean temporary settings for 12 seconds, then exited 143 after intentional SIGTERM |
| macOS crash-dialog boundary | One non-qualifying sandboxed offscreen launch still initialized Touch Bar/AppKit and aborted at 21:08:10; every authoritative executable run was then moved to the sandbox-exempt offscreen path, with no newer crash report |
| Process cleanup | Passed; no product or test process remained after the lifecycle checks |
| Manual desktop interaction | Not run by design; the user requested background validation without visible application windows |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design |

## EtherCATWorkbench optional-Provider presentation qualification

`ISSUE-WB-OPTIONAL-PROVIDER-STATE-001` is a Workbench-private presentation
change. It does not add a Provider, public API, controller connection, online
scan, EtherCAT frame, WKC/DC sample, network access, or persistent value.
`Available` means only that an existing public Provider contract currently
reports available; production names retain the explicit `Local Mock` boundary.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because the old tree reported `Plugin not installed` |
| Absent state | Passed with no installation inference and explicit local-Mock-only text |
| Registered/unavailable state | Passed with the selected public Provider name and unavailable text |
| Available state | Passed with the selected public Provider name; no snapshot, controller, or online state was fabricated |
| Public display-name change | Passed with immediate tree/Details refresh, a neutral shared fallback for whitespace-only names, and no Details-page reconstruction |
| Multi-Provider selection | Passed with the same scored Scan/Diagnostics producer supplying the copied data and visible name; stable ID breaks ties |
| Pre-snapshot Diagnostics | Passed with the selected Provider name and a source-neutral Diagnostics state; the separate `MOCK` source label appears only after `snapshot.mock` is available |
| Details and built-in placeholders | Passed for absent, unavailable, available-without-page, restoration, and accessible summary text |
| Search and tooltip consistency | Passed without the obsolete `installed` wording |
| Provider lifecycle cleanup | Passed for removal, re-addition, availability fallback, stale-overlay removal, stable selection, and no model reset |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Direct offscreen renders | 900 x 600 and 1800 x 1200 inspected without clipping, overlap, or scale drift |
| Complete EtherCATWorkbench suite | 37 passed, 0 failed in an LLDB-supervised isolated offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 37, Scan 7, Diagnostics 7; 88 passed, 0 failed in LLDB-supervised isolated processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | All 16 allow-listed plugin dylibs present |
| Enabled offscreen startup | Stable beyond 12 seconds with clean settings; the target then exited on the intentional SIGTERM with LLDB status 15 |
| Explicitly disabled startup | Stable beyond 12 seconds with `-noload EtherCATWorkbench`; the target then exited on the intentional SIGTERM with LLDB status 15 |
| Headless harness setup | A strong-symbol interposer failed before initialization and a weak-symbol interposer was rejected after its inherited DYLD setting aborted arm64e tool-probe children; qualifying runs instead use a process-local LLDB breakpoint to return from `Utils::TouchBar::setApplicationTouchBar()` |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process and no ReportCrash event or diagnostic report after the final clean breakpoint qualification began at 08:14:00 on 2026-07-18 |
| Manual desktop interaction | Not run by design; all qualifying executable checks were sandbox-exempt and offscreen with no visible window |
| `WITH_TESTS=ON` full product build | Not rerun; this Workbench-only issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; qbs 3.2.0 is installed outside `PATH`, and this issue changes no CMake or qbs file |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design |

The implementation build used:

```text
cmake --build /Users/kvell/kk-project/qt-project/qt-creator-build-ethercat-core-qt611 --target EtherCATWorkbench --parallel 2
```

Each executable test used the test-build `Embed Labs` binary with
`QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, and
`-no-crashcheck` under LLDB supervision. The launcher explicitly removed
`DYLD_INSERT_LIBRARIES`, `DYLD_LIBRARY_PATH`, and `DYLD_FRAMEWORK_PATH`. A
process-local breakpoint returned from
`Utils::TouchBar::setApplicationTouchBar()` and continued the target, because
the current macOS Core otherwise initializes AppKit even with the offscreen
QPA. This debugger state is not inherited by child processes and is not a
repository or product change. The focused test added
`EtherCATWorkbench,testOptionalProviderAvailabilityPresentation`; the 2x run
also set `QT_SCALE_FACTOR=2`. The isolated regression command was executed once
for each of `EtherCATCore`, `EtherCATProject`, `EtherCATDevices`,
`EtherCATWorkbench`, `EtherCATScan`, and `EtherCATDiagnostics`. The test command
shape was:

```text
/usr/bin/env -u DYLD_INSERT_LIBRARIES -u DYLD_LIBRARY_PATH \
  -u DYLD_FRAMEWORK_PATH /usr/bin/xcrun lldb --batch \
  -o 'settings set target.env-vars QT_QPA_PLATFORM=offscreen CRASH_REPORTER_DISABLE=1' \
  -o 'breakpoint set --name _ZN5Utils8TouchBar22setApplicationTouchBarEv' \
  -o 'breakpoint command add 1 -o "thread return" -o "continue"' \
  -o run -k 'thread backtrace all' -- \
  <test-build>/Embed\ Labs.app/Contents/MacOS/Embed\ Labs \
  -no-crashcheck -test <plugin>[,<test-function>]
```

The product build used:

```text
cmake --build /Users/kvell/kk-project/qt-project/qt-creator-build-ethercat-product-qt611 --parallel 2
```

The enabled and disabled product processes used the same clean LLDB breakpoint
boundary and fresh temporary settings under
`/private/tmp/embed-labs-source-neutral-product-enabled.LF8jy5` and
`/private/tmp/embed-labs-source-neutral-product-disabled.Oi5vJ8`, respectively;
the disabled command additionally used `-noload EtherCATWorkbench`. Each target
remained stable beyond 12 seconds, then received SIGTERM directly and exited
with LLDB status 15. Both LLDB sessions exited normally, left no process, and
generated no ReportCrash event or diagnostic report.

Two earlier interposer experiments are non-qualifying harness failures. The
strong-symbol build was rejected before application initialization with exit
134. The weak-symbol build allowed the arm64 target to start, but its inherited
`DYLD_INSERT_LIBRARIES` setting reached system `clang` and `clang++` probes,
which require arm64e. Their dyld reports name Embed Labs as the responsible
parent, explaining the misleading macOS crash dialog. No qualifying run uses
that library, and final clean breakpoint runs generated no report after 08:14:00
on 2026-07-18.

## EtherCATWorkbench invalid-project presentation qualification

`ISSUE-WB-INVALID-PROJECT-PRESENTATION-001` is a Workbench-private
truthfulness and lifecycle change based on local baseline
`53a1ca94ed4b31a9703ecb99fdb7c6f483c44ddd`. It does not modify Project,
ProjectExplorer, Core, persistence, or hardware behavior.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because the old invalid-project root reported `Offline` |
| Invalid tree state | Passed with `Invalid project \| Offline data unavailable`, standard critical icon, real parser error in Search/tooltip, and first-issue routing |
| Recovery topology boundary | Passed with one enabled/non-selectable recovery row and no generated Target, Master, slave, Diagnostics node, or drop target |
| Recovery ID boundary | Passed with `Copy Node ID` disabled, absent from the invalid-root context menu, and guarded against direct signal invocation; valid-project copy restores normally |
| General page | Passed with file-derived display name, full parser error, and unavailable fallback ID/format/creator/migration/modified/Target/Master/count fields |
| Activation ownership | Passed with the Workbench command disabled/rejected; an externally active invalid project preserves both active and invalid facts |
| Lifecycle cleanup | Passed for valid/invalid coexistence, invalid close, stable selection and Details cleanup, valid active fallback, and final close |
| Model consistency | The source model passed under `QAbstractItemModelTester` from pre-open through final close; the proxy model passed from construction through filtering, later active-state, and close changes |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Direct offscreen renders | Tree and Details passed visual inspection at 900 x 600 and 1800 x 1200 without clipping, overlap, or scale drift |
| Complete EtherCATWorkbench suite | 38 passed, 0 failed in an LLDB-supervised isolated offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 38, Scan 7, Diagnostics 7; 89 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | All 16 allow-listed plugin dylibs present |
| Enabled offscreen startup | Stable beyond 15 seconds with fresh settings; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable beyond 15 seconds with `-noload EtherCATWorkbench`; intentional SIGTERM produced LLDB target status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process and no new DiagnosticReports or ReportCrash event after 09:15:00 on 2026-07-18 |
| Corrupt-project Project task | Existing nonfatal ProjectExplorer TaskHub soft assertion remains outside this Workbench-only issue; all qualifying tests still exit 0 |
| Manual desktop interaction | Not run by design; all qualification is offscreen and creates no on-screen main window |
| `WITH_TESTS=ON` full product build | Not rerun; this Workbench-only issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; qbs 3.2.0 is installed outside `PATH`, and this issue changes no CMake or qbs file |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design |

Every qualifying product or test executable run explicitly removed `DYLD_INSERT_LIBRARIES`,
`DYLD_LIBRARY_PATH`, and `DYLD_FRAMEWORK_PATH`, set
`QT_QPA_PLATFORM=offscreen` and `CRASH_REPORTER_DISABLE=1`, and used only a
process-local LLDB breakpoint to return from
`Utils::TouchBar::setApplicationTouchBar()`. No interposer, repository hook,
product dependency, on-screen window, network, or hardware access was used. The
enabled settings path was
`/private/tmp/embed-labs-invalid-product-enabled-qualified.qoIsYV`; the
disabled path was
`/private/tmp/embed-labs-invalid-product-disabled-qualified.DnhDUc`.

## EtherCATWorkbench project-scoped Diagnostics qualification

`ISSUE-WB-DIAGNOSTICS-CONTEXT-001` is a Workbench-private action and
navigation correction based on local baseline
`db547ec8597c0db0d0cf5d3a0f097405dd24ca71`. It changes no Diagnostics
Provider, project data, persistence, online state, or hardware behavior.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because `Open Diagnostics` remained enabled for an invalid selected project |
| Independent-review failure | The added unknown-selection regression first produced 2 passed, 1 failed because tree clearing rewrote the unknown ID as null; blocking that feedback fixed it |
| Invalid-project command state | Base action and Workbench context action are disabled while the invalid root is selected |
| Invalid-project direct execution | Passed; direct `openDiagnosticsRequested` leaves the invalid stable selection and current tree row unchanged |
| Unknown non-model selection | Passed; a non-null unknown `NodeId` clears the stale tree row, disables base/context actions, and makes direct execution a no-op |
| Valid-project exact routing | Passed with two valid projects; selecting the non-first project opens its own Diagnostics branch rather than the wildcard result |
| Projectless fallback | Passed after invalid-project close; a null selection retains the existing first-available Diagnostics lookup |
| Placeholder context menu | Passed; node actions are restricted while an unselectable placeholder menu is open, then the valid stable selection plus matching Diagnostics and copy action states are restored on close |
| Staged-review completion | The first full-suite run exposed an obsolete copy-action expectation; its early test return skipped manual widget cleanup and LLDB caught the resulting test-only stale connection, while the corrected focused command test and final suite exit 0 |
| Command identity and menu context | Passed with the registered command and its Workbench-context action checked in both invalid and valid contexts |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Direct offscreen renders | Tree and Details inspected at 900 x 600 and 1800 x 1200 without clipping, overlap, or scale drift |
| Complete EtherCATWorkbench suite | 38 passed, 0 failed in an LLDB-supervised isolated offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 38, Scan 7, Diagnostics 7; 89 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | All 16 allow-listed plugin dylibs present |
| Enabled offscreen startup | Stable beyond 15 seconds with fresh settings; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable beyond 15 seconds with `-noload EtherCATWorkbench`; intentional SIGTERM produced LLDB target status 15 |
| Initial shutdown regression | LLDB caught a post-assertion `EXC_BAD_ACCESS`; guarding the released `SelectionService` fixed it, all final tests exit 0, and lifecycle runs end only by intentional SIGTERM status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process and no new DiagnosticReports or ReportCrash event after 18:55 on 2026-07-18 |
| Corrupt-project Project task | Existing nonfatal ProjectExplorer TaskHub soft assertion remains outside this Workbench-only issue; all qualifying tests still exit 0 |
| Manual desktop interaction | Not run by design; all executable qualification is offscreen and creates no on-screen main window |
| `WITH_TESTS=ON` full product build | Not rerun; this Workbench-only issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; qbs 3.2.0 is installed outside `PATH`, and this issue changes no CMake or qbs file |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design |

Every qualifying executable explicitly removed `DYLD_INSERT_LIBRARIES`,
`DYLD_LIBRARY_PATH`, and `DYLD_FRAMEWORK_PATH`, set
`QT_QPA_PLATFORM=offscreen` and `CRASH_REPORTER_DISABLE=1`, and used only a
process-local LLDB breakpoint to return from
`Utils::TouchBar::setApplicationTouchBar()`. The enabled product settings were
under
`/private/tmp/embed-labs-diag-context-product-enabled-commit.qdSCy8/settings`;
the explicitly disabled settings were under
`/private/tmp/embed-labs-diag-context-product-disabled-commit.yzNE4o/settings`.
No interposer, repository hook, visible window, network, or hardware access
was used.

## EtherCATWorkbench tree-row accessibility qualification

`ISSUE-WB-TREE-ROW-A11Y-001` is a Workbench-private item-model presentation
change based on local baseline
`cf738be9d320ece891b9e07436aed8a4c9563f30`. It adds no controller connection,
online state, EtherCAT frame, network access, or physical-hardware result.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because the Master row returned an empty `AccessibleTextRole` |
| Accessible text | Passed for name and status cells using the exact current Display text |
| Accessible description | Passed for complete truthful tooltip content, including active-project state, invalid-project parser error, ESI Vendor/Product/Revision identity, scan differences, Diagnostics errors, Provider state, and offline drag/drop guidance |
| Dynamic role notification | Passed for active-project and Scan/Diagnostics changes with both standard accessibility roles in explicit `dataChanged` role lists; existing empty role lists retain Qt's all-roles meaning |
| Real view path | Passed through the Workbench `QSortFilterProxyModel`, not only by direct source-model reads |
| Provider lifecycle | Passed for dynamic data, removal restoration, and a scope guard that removes test Providers even after a future early assertion |
| Failure-first harness cleanup | LLDB contained the first early-return test-fixture shutdown access fault; no product path changed and no DiagnosticReports or ReportCrash entry was generated |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Direct offscreen renders | 1200 x 800 and 2400 x 1600 tree captures inspected without overlap or scale drift; long status remains available through the existing horizontal-scroll contract |
| Manual screen-reader speech | Not claimed; automated evidence covers the standard Qt item-model roles and notifications |
| Complete EtherCATWorkbench suite | 38 passed, 0 failed in an LLDB-supervised isolated offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 38, Scan 7, Diagnostics 7; 89 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | All 16 allow-listed plugin dylibs present |
| Enabled offscreen startup | Stable beyond 15 seconds with fresh settings; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable beyond 15 seconds with `-noload EtherCATWorkbench`; intentional SIGTERM produced LLDB target status 15 |
| Qualified settings | Enabled under `/private/tmp/embed-labs-row-a11y-product-enabled-qualified.mjvPom/settings`; disabled under `/private/tmp/embed-labs-row-a11y-product-disabled-qualified.gHgaZF/settings` |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process and no DiagnosticReports or ReportCrash event after 20:02 on 2026-07-18 |
| Manual desktop interaction | Not run by design; all executable qualification was offscreen and created no visible main window |
| `WITH_TESTS=ON` full product build | Not rerun; this Workbench-only issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; no CMake or qbs file changed |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design |

Qt defines `AccessibleTextRole` and `AccessibleDescriptionRole` as the
standard item data for accessibility clients and screen readers:
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum>. Beckhoff documents tree
status/control information and explicit EtherCAT operational/error states, but
does not define this Qt role mapping:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html> and
<https://infosys.beckhoff.com/content/1033/el6752/2584310027.html>.

## EtherCATWorkbench project-scoped Locate qualification

`ISSUE-WB-LOCATE-CONTEXT-001` is a Workbench-private navigation correction
based on local baseline `a820995521aa9c7a4ffa459eef767c7322674680`. It does
not change Scan or Diagnostics data, Project persistence, controller state,
network behavior, or physical hardware capability.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because `Locate First Topology Difference` remained enabled for clean selected project Beta while Alpha owned the only Mock difference |
| Exact project scope | Passed for project-filtered topology-difference and issue queries, disabled base/context actions on clean Beta, and exact routing inside Alpha |
| Direct request guard | Passed; both controller signals leave Beta selection, current tree row, and Details context unchanged when Beta has no result |
| Unknown stable ID | Passed; an unknown non-null ID clears the stale row, disables both actions, and remains unchanged after either direct request |
| Projectless fallback | Passed for a genuinely null selection and a known Device Repository selection; each retains the existing global first-available lookup |
| Placeholder context menu | Passed with both actions temporarily disabled and Alpha's stable selection plus enabled scoped state restored when the menu closes |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Direct offscreen renders | Tree and Details inspected at 900 x 600 and 1800 x 1200 without clipping, overlap, or scale drift |
| Complete EtherCATWorkbench suite | 39 passed, 0 failed in an LLDB-supervised isolated offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 39, Scan 7, Diagnostics 7; 90 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | All 16 allow-listed plugin dylibs present |
| Enabled offscreen startup | Stable beyond 15 seconds with fresh settings under `/private/tmp/embed-labs-locate-product-enabled.BzmYPj/settings`; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable beyond 15 seconds with `-noload EtherCATWorkbench` and fresh settings under `/private/tmp/embed-labs-locate-product-disabled.2CLrhS/settings`; intentional SIGTERM produced LLDB target status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process and no DiagnosticReports or ReportCrash event after 20:45:00 on 2026-07-18 |
| Manual desktop interaction | Not run by design; all executable qualification was offscreen and created no visible main window |
| `WITH_TESTS=ON` full product build | Not rerun; this private Workbench issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; no CMake or qbs file changed |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design; Scan and Diagnostics remain local Mock Providers |

Qt Creator documents that Projects-view context actions apply to the selected
tree item and project:
<https://doc.qt.io/qtcreator/creator-projects-view.html>. Beckhoff documents
selected-I/O-tree-device context for the EtherCAT Online page and comparative
scans, but does not define this Workbench-private cross-project guard:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446518411.html> and
<https://infosys.beckhoff.com/content/1033/ps2001-2420-1001/10832129675.html>.

## EtherCATWorkbench offline-slave removal confirmation qualification

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-CONFIRM-001` is a Workbench-private safety
correction based on local baseline
`03a6abd1c696d34acae685bdf0c340a9a471a837`. It changes no Project data
contract, persistence format, online state, controller transport, network
behavior, or physical-hardware capability.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because the old `Remove from Offline Master` action deleted immediately and no confirmation appeared |
| Action disclosure | Passed with `Remove from Offline Master...` plus tooltip/status text naming the included Process Data, Startup, and Distributed Clocks configuration |
| Confirmation content | Passed with plain text, stable slave name including literal `%1`/`%2`, one-based position, complete consequence text, Undo guidance, and standard Yes/No buttons |
| Safe default | Passed with `No` as both default and Escape button |
| Cancel boundary | Passed with an identical complete Project snapshot, stable selection, modified state, Undo/Redo availability, and zero `projectChanged` signals |
| Context drift | Passed; changing the stable selection while the question is open, then choosing Yes, removes no slave and preserves the new selection |
| Project close during question | Passed; choosing Yes after the project has closed submits no stale replacement and leaves no stale tree row |
| Confirmed removal | Passed for the captured slave only, normalized remaining position, nearest-node selection repair, and Project-owned Undo/Redo |
| Complete Undo/Redo restoration | Passed for remove, complete Undo of the full prior slave list, Redo of the exact removal, and a final complete Undo, including stable IDs, identity, Alias, ESI reference, Process Data, Startup, and DC configuration |
| QAction and context-menu identity | Passed through the existing ActionManager command and real repository/slave context menus; no parallel callback was added |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Direct offscreen renders | 400 x 151 and 552 x 422 question captures inspected without clipping, overlap, or scale drift |
| Complete EtherCATWorkbench suite | 39 passed, 0 failed in an LLDB-supervised isolated offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 39, Scan 7, Diagnostics 7; 90 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | All 16 allow-listed plugin dylibs present |
| Enabled offscreen startup | Stable for about 42 seconds with fresh settings under `/private/tmp/embed-labs-remove-authoritative-enabled-settings`; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable for about 42 seconds with `-noload EtherCATWorkbench` and fresh settings under `/private/tmp/embed-labs-remove-authoritative-disabled-settings`; intentional SIGTERM produced LLDB target status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process and no DiagnosticReports or ReportCrash event after 21:48:00 on 2026-07-18 |
| Manual desktop interaction | Not run by design; all executable qualification was offscreen and created no visible main window |
| `WITH_TESTS=ON` full product build | Not rerun; this private Workbench issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; no CMake or qbs file changed |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design; Scan and Diagnostics remain local Mock Providers |

Qt documents the standard modal question, default button, Escape button, and
plain-text controls used by the confirmation:
<https://doc.qt.io/qt-6/qmessagebox.html>. The dialog uses non-blocking
`open()` and delete-on-close ownership in accordance with Qt's guidance to
avoid `exec()`'s nested event loop:
<https://doc.qt.io/qt-6/qdialog.html#exec>. Beckhoff documents that removing
an I/O device deletes it from both the tree and configuration, but does not
define this product's confirmation interaction:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.

## EtherCATWorkbench repository quick-add target qualification

`ISSUE-WB-ESI-QUICK-ADD-TARGET-DISCLOSURE-001` is a Workbench-private target
safety correction based on local baseline
`b64d0b5f0ac9ae1cf4f5620c942d953301904eed`. It changes no Project data
contract, persistence format, online state, controller transport, network
behavior, or physical-hardware capability.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because the previous fixed `Add to Active Offline Master` text named neither Project nor Master |
| Target disclosure | Passed with compact `Add to "<Project>" / "<Master>"` menu text and tooltip/status text naming the exact target |
| Offline boundary disclosure | Passed; tooltip/status state that only the local offline project changes and no controller or physical hardware is contacted |
| Project lifecycle | Passed for two open projects, active-project switching, Project and Master renames, active close fallback, and final no-project disablement |
| Special-character safety | Passed for literal `%1`, `%2`, and `&`; visible names are preserved and QAction ampersands are escaped |
| Stale-target guard | Passed; changing the active Project after presentation rejects the captured stable Project/Master IDs and preserves both complete Project snapshots |
| Exact mutation target | Passed; the normal QAction changes only the Project and Master named in the action while the other complete Project snapshot remains equal |
| Project-owned Undo | Passed; Undo restores the displayed target's complete previous slave list and name |
| QAction and context-menu identity | Passed through the existing ActionManager command and real Device Repository context menu; no parallel callback or QAction was added |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Direct offscreen renders | 382 x 190 and 764 x 380 menu captures inspected without clipping, overlap, or scale drift |
| Complete EtherCATWorkbench suite | 39 passed, 0 failed in an LLDB-supervised isolated offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 39, Scan 7, Diagnostics 7; 90 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | Exactly the 16 allow-listed plugin dylibs are present; no missing or extra plugin |
| Enabled offscreen startup | Stable for 16 seconds with fresh settings under `/private/tmp/embed-labs-quick-add-product-enabled-qualified.6Ym6xU/settings`; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable for 16 seconds with `-noload EtherCATWorkbench` and fresh settings under `/private/tmp/embed-labs-quick-add-product-disabled-qualified.mOI49C/settings`; intentional SIGTERM produced LLDB target status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process and no DiagnosticReports or ReportCrash event after 22:40:03 on 2026-07-18 |
| Manual desktop interaction | Not run by design; all executable qualification was offscreen and created no visible main window |
| `WITH_TESTS=ON` full product build | Not rerun; this private Workbench issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; no CMake or qbs file changed |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design; Scan and Diagnostics remain local Mock Providers |

Qt Creator documents dynamic command presentation and the command-owned
`QAction`: <https://doc.qt.io/qtcreator-extending/actionmanager.html>. Qt
documents action text, tooltip, status text, and literal ampersand handling:
<https://doc.qt.io/qt-6/qaction.html>. Beckhoff's offline configuration flow
appends a slave beneath the explicitly selected device in the tree:
<https://infosys.beckhoff.com/content/1033/el331x/1036999947.html> and
<https://infosys.beckhoff.com/content/1033/b110_ethercat_optioninterface/2481604363.html>.
The dynamic Project/Master wording and stable-ID stale-target rejection are
Embed Labs Qt-native completions.

## EtherCATWorkbench Details Provider-removal continuity qualification

`ISSUE-WB-DETAILS-PROVIDER-REMOVE-TAB-CONTINUITY-001` is a Workbench-private
lifecycle correction based on local baseline
`fc9dc73c5449935f44446ab3c724e861a04d18a3`. It changes no public Provider
contract, persistent setting, project data, online state, transport, network,
or physical-hardware capability.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because removal of unrelated Provider A reset the actual current key to built-in General instead of the still-valid Provider B key |
| Independent review signal failure | 2 passed, 1 failed because toggling removed but still-live Provider A deleted Provider B's saved widget before the removal-time disconnect was added |
| Independent review ABA failure | 2 passed, 1 failed because the disconnect-only implementation restored stale Provider B over a newer General selection after switch away/back |
| Independent review reentrant failure | 2 passed, 1 failed at that historical baseline because a later direct about-to-remove slot could still enumerate Provider A; the current registry now unlinks it before emitting its removal signal, and the departing-ID marker also excludes queued stale work |
| Unrelated Provider removal | Passed; Provider B remains the current tab after Provider A's widgets are synchronously destroyed and A is unregistered |
| Availability lifecycle | Passed for Provider A unavailable, available again, and then removed while Provider B remains current |
| Selected Provider removal | Passed; removing Provider B removes its widget and falls back to the deterministic first remaining page |
| Context continuity | Passed; stable Selection and Details NodeId remain unchanged across unrelated removal and queued rebuild |
| Unregistered Provider isolation | Passed; toggling removed but still-live Provider A's availability does not recreate or replace Provider B's widget |
| Queued rebuild freshness | Passed; switch away, switch back, and select General before the removal MetaCall drains preserves the newer General selection instead of restoring stale Provider B |
| Object-pool signal re-entry | Passed; a later direct removal slot switches Master to Project and drains MetaCalls before registry removal, but Provider A is never recreated and the final context/key remain current |
| Ownership cleanup | Passed for ordinary removal; no Provider/widget pointer crosses that removal boundary, removed widgets disappear, and scope cleanup unregisters every remaining test Provider |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Direct offscreen renders | 900 x 600 and 1800 x 1200 captures inspected with Provider B selected and no clipping, overlap, or scale drift |
| Complete EtherCATWorkbench suite | 39 passed, 0 failed in an LLDB-supervised isolated offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 39, Scan 7, Diagnostics 7; 90 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | Exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | Stable for 16 seconds with fresh settings under `/tmp/embed-labs-product-current-lifecycle.sxy7zN/enabled/settings`; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable for 16 seconds with `-noload EtherCATWorkbench` and fresh settings under `/tmp/embed-labs-product-current-lifecycle.sxy7zN/disabled/settings`; intentional SIGTERM produced LLDB target status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process, recent DiagnosticReports file, or ReportCrash event at the final 2026-07-18 23:42:25 +0800 audit |
| Manual desktop interaction | Not run by design; all executable qualification was offscreen and created no visible main window |
| `WITH_TESTS=ON` full product build | Not rerun; this private Workbench issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; no CMake or qbs file changed |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design; Scan and Diagnostics remain local Mock Providers |

Qt Creator demonstrates restoring the current tab while replacing Project
settings panels:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L1366-L1385>.
Qt documents the tab contract at
<https://doc.qt.io/qt-6/qtabwidget.html> and the Qt Creator object-pool removal
notification at
<https://doc.qt.io/qtcreator-extending/pluginmanager.html>. Beckhoff documents
that an EtherCAT terminal's available property tabs depend on the selected
device:
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html>.
The value-only PageKey restoration is an Embed Labs Qt-native completion.

The current lifecycle contract supersedes the historical signal-order wording
above. `ProviderRegistry` unlinks the departing object before it emits
`providerAboutToBeRemoved`, although PluginManager still retains the object in
its pool until the notification returns. Ordinary hosted pages are destroyed
before `removeObject()` returns. A Provider that self-unregisters from its own
active page callback is removed from the live-page map immediately. An
already-tabbed active widget may remain attached and alive until callback
return, when it is destroyed; its plugin must remain loaded, including page
destructor and meta-object code, through that callback boundary.

## EtherCATWorkbench context-menu selection-drift qualification

`ISSUE-WB-CONTEXT-MENU-SELECTION-DRIFT-001` is a Workbench-private safety
correction based on local baseline
`63e692ea08d8989b3911c57474ec10242038fc64`. It changes no Project data
contract, persistence format, QAction identity, online state, controller
transport, network behavior, or physical-hardware capability.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because the real middle-slave popup remained open after Selection Service moved to the first slave and the shared Move Down command changed the complete Project snapshot |
| Popup selection boundary | Passed; any different stable `NodeId` closes the open menu synchronously while the new selection remains authoritative |
| Menu-owned selection change | Passed with the existing Open Diagnostics QAction triggered from the real popup; the Diagnostics NodeId remains selected after the menu returns |
| Shared QAction identity | Passed with the existing ActionManager Move Down command from the real configured-slave popup; no copied, proxy, or per-popup mutation action was added |
| Project mutation boundary | Passed with the complete Project snapshot and Undo/Redo availability unchanged after selection drift |
| Post-menu restoration | Passed with the tree row and all shared command states synchronized to the latest selection rather than writing the opening selection back |
| Placeholder compatibility | Passed through the existing command suite; a placeholder popup retains the prior stable selection as its baseline and restores its command state on close |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Complete EtherCATWorkbench suite | 39 passed, 0 failed in an isolated LLDB-supervised offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 39, Scan 7, Diagnostics 7; 90 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | Exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | Stable beyond 16 seconds with fresh settings under `/tmp/embed-labs-context-product-enabled-qualified.U1AliL/settings`; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable beyond 16 seconds with `-noload EtherCATWorkbench` and fresh settings under `/tmp/embed-labs-context-product-disabled-qualified.PslMuR/settings`; intentional SIGTERM produced LLDB target status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process, new DiagnosticReports file, or related ReportCrash event at the final 2026-07-19 00:28:45 +0800 audit |
| Visual/manual desktop inspection | Not run by design; the change adds no geometry and all executable qualification was offscreen with no visible main window |
| `WITH_TESTS=ON` full product build | Not rerun; this private Workbench issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; no CMake or qbs file changed |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design; Scan and Diagnostics remain local Mock Providers |

Qt documents the synchronous popup and normal action-signal behavior at
<https://doc.qt.io/qt-6/qmenu.html#exec>. Qt Creator documents the shared
user-facing `Command::action()` contract at
<https://doc.qt.io/qtcreator-extending/actionmanager.html> and retains
Project-tree context-menu focus until its popup hides:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projecttree.cpp#L337-L395>.
Beckhoff binds its device context menu and Remove behavior to the selected I/O
device:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.
The exact close-on-drift rule is an Embed Labs Qt-native completion.

## EtherCATWorkbench navigation expansion-continuity qualification

`ISSUE-WB-NAV-EXPANSION-CONTINUITY-001` is a Workbench-private presentation
correction based on local baseline
`d64e159164597d234027fd479288bfc49860685d`. It changes no Project data
contract, persistence format, online state, controller transport, network
behavior, or physical-hardware capability.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed because the old unconditional post-reset `expandToDepth(2)` reopened the deliberately collapsed Alpha Project |
| Surviving collapsed state | Passed; an unrelated Alpha Project remains collapsed after another Project's renamed snapshot resets the model |
| Surviving deep expansion | Passed; the stable RxPDO group and PDO remain expanded through reset by existing `NodeId` values |
| Stable selection | Passed; Master `NodeId`, tree current row, and Selection Service remain aligned after reset |
| New Project default | Passed; a newly introduced Project, Target, and Master retain the established depth-two default expansion |
| Temporary filter | Passed; matching branches expand without changing normal state, and clearing the filter restores the pre-filter collapsed Project |
| Dynamic filter match | Passed; a matching ESI device inserted after an empty result has an expanded parent and a visible tree rectangle |
| External Selection | Passed; choosing a filtered-out stable PDO clears the filter and expands its ancestor path without changing identity |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 |
| Complete EtherCATWorkbench suite | 40 passed, 0 failed in an isolated LLDB-supervised offscreen process |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 40, Scan 7, Diagnostics 7; 91 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | Exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | Stable for 16 seconds with fresh settings under `/tmp/embed-labs-nav-product-enabled-final.ZyWuX2/settings`; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable for 16 seconds with `-noload EtherCATWorkbench` and fresh settings under `/tmp/embed-labs-nav-product-disabled-final.UiBc9t/settings`; intentional SIGTERM produced LLDB target status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process, new DiagnosticReports file, or related ReportCrash event at the final 2026-07-19 01:06:31 +0800 audit |
| Manual desktop inspection | Not run by design; all executable qualification was offscreen and this issue adds no geometry |
| `WITH_TESTS=ON` full product build | Not rerun; this private Workbench issue does not touch the known EasyBoard test include blocker |
| qbs execution | Not run; no CMake or qbs file changed |
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Public API, dependency, persistence, source-list, CMake, or qbs changes | None |
| Network or physical hardware access | Not performed by design; Scan and Diagnostics remain local Mock Providers |

Qt exposes tree expansion state through `QTreeView::expanded` and
`QTreeView::collapsed`:
<https://doc.qt.io/qt-6/qtreeview.html#expanded>. Qt's reset lifecycle says
old indexes and selection information become invalid:
<https://doc.qt.io/qt-6/qabstractitemmodel.html#beginResetModel>. Qt Creator
20.0's Project tree records expansion changes and requests semantic expansion
again after its model rebuilds:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projecttreewidget.cpp#L291-L302>,
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L486-L496>,
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L526-L539>.
Beckhoff documents the device and process/status hierarchy under I/O / Devices:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html>.
The exact reset/filter restoration rule is an Embed Labs Qt-native completion.

## EtherCATWorkbench Details focus-continuity qualification

`ISSUE-WB-DETAILS-FOCUS-CONTINUITY-001` is a Details lifecycle correction
based on local baseline
`0637955df9ab009bb0ee1fcb892dc74e5ede27e7`. The focus token and operation
anchors are private value types; no public Provider method, project format,
persistent setting, controller transport, network behavior, or
physical-hardware capability changes.

| Check | Result |
|---|---|
| Failure-first focus regression | Reproduced; a context rebuild recreated the same semantic General page but moved focus from `EtherCATProjectGeneralCreatedBy` to the tab bar |
| Value-only focus identity | Passed with stable `NodeId`, semantic `PageKey`, child `objectName`, and rebuild generation; no widget, model index, or Provider pointer is retained |
| Exact recreated-widget focus | Passed for the same PageKey under distinct NodeIds; the old widget is destroyed and the matching child in the new widget receives focus |
| Focus fallback | Passed for a missing or hidden named target and for the keyboard-reachable empty-state page |
| External focus | Passed; focus moved outside Details is not stolen by a rebuild, refresh, or posted continuation |
| Direct tab-bar focus | Passed; explicit tab-bar focus cancels page-child restoration and remains sticky |
| Unified operation pump | Passed for rebuild/refresh serialization, duplicate request coalescing, refresh self-trigger, nested availability/context/update/clear/delete re-entry, and posted continuation after eight synchronous operations |
| Yield-time user priority | Passed; real `QTabBar::currentChanged`, `tabBarClicked`, and focus choices made after a yield remain authoritative over the older transaction anchor |
| Registry removal order | Passed in Core; the departing Provider is already absent from registry enumeration when `providerAboutToBeRemoved` is emitted, while it remains in the PluginManager pool until notification return |
| Nested Provider removal | Passed for consecutive removal, direct signal re-entry, queued MetaCall draining, page-hide re-entry, and provider removal triggered by another Provider's update |
| Ordinary page lifetime | Passed; all ordinary hosted pages from a departing Provider are destroyed before `PluginManager::removeObject()` returns |
| Active-callback self-unregister | Passed; the active widget remains alive through its Provider callback, is not destroyed inside `removeObject()`, and is destroyed after callback return before the next queued MetaCall |
| Plugin unload boundary | Contract recorded; a self-unregistering Provider/plugin must keep page destructor and Qt meta-object code loaded until its active callback returns |
| Stale snapshot isolation | Passed; a Provider removed during another callback is not updated later from an earlier registry snapshot |
| Normal-scale Workbench suite | 41 passed, 0 failed |
| `QT_SCALE_FACTOR=2` Workbench suite | 41 passed, 0 failed |
| Direct offscreen renders | Four frozen captures under `/tmp/embed-labs-details-render-final2.wMQmxh` match the inspected normal/2x artifacts exactly and show no clipping, overlap, or scale drift |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 41, Scan 7, Diagnostics 7; 92 passed, 0 failed under `/tmp/embed-labs-six-suites-final2.XeKLz8` |
| Qualified Qt and test build | Qt 6.11.0 Release; required EtherCAT targets passed in `qt-creator-build-ethercat-core-qt611` |
| Unrelated all-target tests-on build | Remains blocked by the pre-existing `easyboardbrowser.cpp` include of unavailable `extensionmanager_test.h`; it is not claimed as successful evidence |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | Exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | Stayed alive for 16 seconds under `/tmp/embed-labs-product-lifecycle-frozen.NIAjdQ`; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stayed alive for 16 seconds with `-noload EtherCATWorkbench` under `/tmp/embed-labs-product-lifecycle-frozen.NIAjdQ`; intentional SIGTERM produced LLDB target status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process and no new Embed Labs crash report; the only running ReportCrash agent pre-dated qualification by more than one day and was unrelated |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or interposer |
| qbs execution | Not run; no CMake or qbs file changed |
| Product-owned files | Existing EtherCATCore ProviderRegistry/tests and EtherCATWorkbench Details/tests only; `src/plugins/ethercatcore` is product-owned |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network or physical hardware access | Not performed; Scan and Diagnostics remain local Mock Providers |

Qt defines the tab and focus behavior used by the regression at
<https://doc.qt.io/qt-6/qtabwidget.html> and
<https://doc.qt.io/qt-6/qwidget.html>. Qt Creator 20.0 restores semantic Project
settings panels after replacing their widgets:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L1366-L1385>.
Beckhoff documents selection-dependent EtherCAT terminal tabs at
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html>.
The exact value-only focus token, bounded pump, user-choice priority, and
re-entry-safe Provider lifecycle are Embed Labs Qt-native completions.

## EtherCATWorkbench CoE filter empty-state qualification

`ISSUE-WB-COE-FILTER-EMPTY-A11Y-001` is based on local baseline
`6935b103b1adbc3641c28afcd3e683f2772848fb`.

| Check | Result |
|---|---|
| Failure-first focused test | 2 passed, 1 failed under `/tmp/embed-labs-coe-filter-failure.UBAZUx` because a zero-result CoE filter still presented a blank tree instead of an explicit empty state |
| Text and advanced filters | Passed for Unicode/value text, object range, Hide Standard, and Hide PDO filters |
| Dynamic Value edit | Passed; editing the only matching Mock Value removes the proxy row and immediately presents the empty state |
| Clear Filters transaction | Passed; text, range, Hide Standard, and Hide PDO are reset together without exposing an intermediate selection as new user intent |
| Stable selection | Passed using the private numeric `AddressRole`; the previous address is restored when visible and no model index or item pointer is retained |
| Keyboard recovery | Passed; Space activates `Clear Filters`, the dictionary returns, and focus moves to the restored dictionary row |
| Empty-state action safety | Passed; `Add to Startup` is disabled with zero matches and returns only for the restored writable Mock object |
| Accessibility | Passed for nonempty filter name/description, empty-state name/description, message name, and clear-button description |
| Focused normal-scale test | 3 passed, 0 failed; exit 0 under `/tmp/embed-labs-coe-filter-locked-normal2.vocRvH` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; exit 0 under `/tmp/embed-labs-coe-filter-locked-2x2.iFfDMA` |
| Direct offscreen renders | Inspected at 1100 x 760 and 2200 x 1520; no clipping, overlap, or scale drift; SHA-256 `dfcc0dfab520ee5013ea22a561f9f89e1a5b28cbc6c8e11b037db48ebed87440` and `3563edf87c993042cb51139f358937225722d077487509ea749fa11aa5eea55c` |
| Complete EtherCATWorkbench normal scale | 41 passed, 0 failed; exit 0 under `/tmp/embed-labs-workbench-post-address-normal.1q6GwZ` |
| Complete EtherCATWorkbench 2x scale | 41 passed, 0 failed; exit 0 under `/tmp/embed-labs-workbench-post-address-2x.FPJYWy` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 41, Scan 7, Diagnostics 7; 92 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Product version inventory | Exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | Stable for 16 seconds under `/tmp/embed-labs-coe-product-locked-enabled.KY0v5e`; intentional SIGTERM produced LLDB target status 15 |
| Explicitly disabled startup | Stable for 16 seconds with `-noload EtherCATWorkbench` under `/tmp/embed-labs-coe-product-locked-disabled.YHhQUP`; intentional SIGTERM produced LLDB target status 15 |
| Process and crash-report cleanup | No residual Embed Labs or LLDB process and no new Embed Labs DiagnosticReports file |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or interposer |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network or physical hardware access | Not performed; CoE remains a local Mock/offline page and the existing Add-to-Startup ProjectService path is unchanged |

Qt defines the dynamic proxy and accessibility contracts at
<https://doc.qt.io/qt-6/qsortfilterproxymodel.html> and
<https://doc.qt.io/qt-6/qwidget.html>. Beckhoff's CoE Online page remains the
object-dictionary comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.
The atomic clear and address-based restoration are Embed Labs Qt-native
behavior.

## EtherCATWorkbench insert-device target-lifecycle qualification

`ISSUE-WB-INSERT-DIALOG-TARGET-LIFECYCLE-001` uses local baseline
`a0b68e7313b80e4292a135966f7ead4c4e131656`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Setup and cleanup passed; the old implementation failed because an active-project switch did not reject the open insert dialog |
| Active Project changes | Dialog rejects immediately; neither Project snapshot is mutated |
| Target Project closes | `projectAboutToBeRemoved` rejects the dialog before the target disappears |
| Target Master disappears or Project invalidates | The matching `projectChanged` snapshot rejects the dialog when the stable Master is absent or the Project is invalid |
| Duplicate lifecycle notifications | Visibility guard limits rejection to one `rejected` signal and prevents late error presentation |
| Focused normal-scale test | Six passed, zero failed: setup, four invalidation rows, and cleanup |
| Focused 2x-scale test | Six passed, zero failed with `QT_SCALE_FACTOR=2` |
| Complete EtherCATWorkbench suite | 45 passed, zero failed |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 45, Scan 7, Diagnostics 7; 96 passed, zero failed |
| Product build | `WITH_TESTS=OFF` passed in `qt-creator-build-ethercat-product-qt611` |
| Enabled offscreen startup | Stable for more than 30 seconds; stopped intentionally under LLDB |
| Explicitly disabled startup | Stable for more than 30 seconds with `-noload EtherCATWorkbench`; one non-fatal shared-memory initialization message did not prevent startup; stopped intentionally under LLDB |
| Process and crash-report cleanup | No residual Embed Labs process and no new Embed Labs DiagnosticReports file |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no main window or crash dialog became visible to the user |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network or physical hardware access | Not performed; the workflow remains an offline Project mutation |

Qt's dialog and connection-lifetime contracts are documented at
<https://doc.qt.io/qt-6/qdialog.html> and
<https://doc.qt.io/qt-6/qobject.html>. Qt Creator's Project removal precedent
is the immediate deregistration in
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L1509-L1515>.
Beckhoff's selected-target insertion flow is documented at
<https://infosys.beckhoff.com/content/1033/eap/1521664395.html>.

## EtherCATWorkbench Process Data table accessibility qualification

`ISSUE-WB-PROCESS-DATA-TABLE-A11Y-001` uses local baseline
`1ce66e3332e17f0ab3e6e4597734d0339dfad230`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Setup and cleanup passed; the old implementation failed because `EtherCATProcessDataSyncManagers` had an empty accessible name under `/private/tmp/embed-labs-process-a11y-failure.ooJLPR` |
| Five table widgets | Unique translated accessible names and nonempty purpose descriptions verified for Sync Manager, PDO Assignment, PDO List, PDO Content, and Process Image Preview |
| Standard item roles | Every populated cell provides `Qt::AccessibleTextRole` and a nonempty `Qt::AccessibleDescriptionRole` containing its column heading and accessible value when nonempty |
| Row and operation context | Descriptions include the row Name where available and retain selection, assignment, unsupported/mandatory, automatic-offset, and absolute bit-range guidance; mandatory and unsupported reasons take precedence, while ordinary read-only contexts never advertise selection |
| Assignment checkbox | Passed for both states; the visually empty Assigned column reports `Assigned` or `Not assigned` while `Qt::CheckStateRole` and existing edit flags remain unchanged |
| Long-cell recovery | Complete Unicode PDO and PDO Entry names with 256-character suffixes are preserved in accessible text, descriptions, and tooltips without changing table geometry or elision policy |
| Empty display values | Passed; `Qt::AccessibleTextRole` remains a valid `QString` value even when the visual cell is intentionally empty |
| Focused normal-scale test | 3 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-process-a11y-final3-normal.kZTfM1` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-process-a11y-final3-2x.rpWLPm` |
| Complete EtherCATWorkbench normal scale | 46 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-workbench-final2-normal.d8z7g7` |
| Complete EtherCATWorkbench 2x scale | 46 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-workbench-final2-2x.GACIqx` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 46, Scan 7, Diagnostics 7; 97 passed, 0 failed in isolated LLDB-supervised processes |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611` |
| Product inventory | Exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | Delayed initialization completed and the process stayed alive for 31 seconds under `/private/tmp/embed-labs-product-enabled-final5.G4bRdZ`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | Delayed initialization completed and the process stayed alive for 31 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-product-disabled-final5.oaSTLm`; intentional SIGTERM produced target status 15; one non-fatal shared-memory message did not interrupt startup |
| Process and crash-report cleanup | No forced kill, residual target/LLDB process, or new Embed Labs/LLDB DiagnosticReports file |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| Assistive-technology claim | Standard Qt widget/item-model metadata verified; no manual VoiceOver reading is claimed |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, custom model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network or physical hardware access | Not performed; Process Data remains local Project/ESI configuration and validation |

Qt's standard item accessibility and widget metadata contracts are documented
at <https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleName-prop>. Qt Creator 20.0's
explicit accessibility precedents are
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247>
and
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597>.
Beckhoff's Process Data comparison is
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.

## EtherCATWorkbench topology dialog bounds qualification

`ISSUE-WB-TOPOLOGY-DIALOG-BOUNDS-001` uses local baseline
`9e56cfebbd09766ea7ae326b5741acb95fecb274`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Setup and cleanup passed; the old implementation failed because a valid long-name topology dialog was 13235 logical pixels wide on an 800-pixel available screen under `/private/tmp/embed-labs-topology-bounds-failure.UNq9D8` |
| Screen boundary | Initial client and frame geometry stay inside the current Workbench page screen's `availableGeometry()` with token-based work-area insets |
| Full offline values | Two Chinese/Unicode slave names with 512-character payloads are preserved verbatim in Name; the first complete name is also preserved in the second row's Previous cell |
| Horizontal recovery | `ResizeToContents` and `ElideNone` remain; the as-needed horizontal scrollbar has a positive range, the first column is visible at minimum, and Status is visible at maximum |
| Dialog operation | Summary, ten columns, Offline/Not modeled boundaries, and a visible enabled Close button remain intact |
| Focused normal-scale test | 3 passed, 0 failed under `/private/tmp/embed-labs-topology-bounds-qualified-normal.3ci48Z`; render is 784 by 279 pixels |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed under `/private/tmp/embed-labs-topology-bounds-qualified-2x.RUpPcy`; render is 768 by 558 pixels |
| Complete EtherCATWorkbench normal scale | 47 passed, 0 failed under `/private/tmp/embed-labs-topology-workbench-qualified-normal.P95AUC` |
| Complete EtherCATWorkbench 2x scale | 47 passed, 0 failed under `/private/tmp/embed-labs-topology-workbench-qualified-2x.OJXAsW` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 47, Scan 7, Diagnostics 7; 98 passed, 0 failed in isolated LLDB-supervised offscreen processes |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611` |
| Product inventory | Exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | Process stayed alive for 33 seconds under `/private/tmp/embed-labs-topology-product-enabled-final2.IuIojz`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | Process stayed alive for 32 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-topology-product-disabled-final.9EKJ0H`; intentional SIGTERM produced target status 15; one non-fatal shared-memory message did not interrupt startup |
| Process and crash-report cleanup | No residual target/LLDB process and no new Embed Labs/LLDB DiagnosticReports file |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, online data, physical-port model, or hardware access | Not performed or added; Topology remains a read-only offline Project view |

Qt's sizing and scrolling contracts are documented at
<https://doc.qt.io/qt-6/qheaderview.html#ResizeMode-enum>,
<https://doc.qt.io/qt-6/qscreen.html#availableGeometry-prop>, and
<https://doc.qt.io/qt-6/qabstractscrollarea.html#horizontalScrollBarPolicy-prop>.
Qt Creator's local centered Locator popup uses the parent widget's screen when
the popup is not yet visible. Beckhoff's selected-master Topology entry is
documented at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446515467.html>.

## EtherCATWorkbench Startup table accessibility qualification

`ISSUE-WB-STARTUP-TABLE-A11Y-001` uses local baseline
`cf3c437147e816320256c1e75b5c67863de77663`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Setup and cleanup passed; the old implementation failed because `EtherCATStartupTable` had an empty accessible name under `/private/tmp/embed-labs-startup-a11y-failure.kptBc4` |
| Table widget | Translated accessible name and nonempty purpose description verified |
| Standard item roles | All nine columns return an actual `QString` through `Qt::AccessibleTextRole`; non-Enabled text exactly matches the complete Display value; description and tooltip contain object address, heading, and full value |
| Enabled checkbox | Passed for Enabled and Disabled text while the intentionally empty Display value, `Qt::CheckStateRole`, and checkable flags remain unchanged |
| Long-cell recovery | Complete 256-byte raw Data and long Chinese/Japanese/Unicode Comment values remain available in accessible text, description, and tooltip without geometry changes |
| Truthful operation guidance | Passed for editable configured-slave fields, read-only CoE Protocol, read-only repository catalogue requests, and fixed ESI requests that cannot be enabled/disabled, edited, deleted, or moved |
| Focused normal-scale test | 3 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-startup-a11y-final-tests.KbokZb/focused-normal` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-startup-a11y-final-tests.KbokZb/focused-2x` |
| Complete EtherCATWorkbench normal scale | 48 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-startup-a11y-final-tests.KbokZb/full-normal` |
| Complete EtherCATWorkbench 2x scale | 48 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-startup-a11y-final-tests.KbokZb/full-2x` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 48, Scan 7, Diagnostics 7; 99 passed, 0 failed with target exit 0 in isolated LLDB-supervised processes under `/private/tmp/embed-labs-startup-a11y-six-final.mI2ZSV` |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611` |
| Product inventory | Exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | After PID detection, the process stayed alive for a measured 35 seconds under `/private/tmp/embed-labs-startup-a11y-product-enabled-final.yjyQ90`; `lifecycle-evidence.txt` records epoch/UTC boundaries and intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | After PID detection, the process stayed alive for a measured 35 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-startup-a11y-product-disabled-final.FFNwfM`; the evidence file records epoch/UTC boundaries, intentional SIGTERM produced target status 15, and one non-fatal shared-memory message did not interrupt startup |
| Process and crash-report cleanup | No residual target/LLDB process and no new Embed Labs/LLDB DiagnosticReports file |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| Assistive-technology claim | Standard Qt widget/item-model metadata verified; no manual VoiceOver reading is claimed |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, custom model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, SDO/controller transport, online state, or hardware access | Not performed or added; Startup remains offline Project/ESI configuration and no request was sent |

Qt's standard item and widget accessibility contracts are documented at
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleName-prop>. Qt Creator 20.0's
local precedents are
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/fancymainwindow.cpp#L244-L247>
and
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/terminal/terminalpane.cpp#L586-L597>.
Beckhoff's ordered Startup request comparison is documented at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.

## EtherCATWorkbench CoE dictionary cell accessibility qualification

`ISSUE-WB-COE-DICTIONARY-CELL-A11Y-001` uses local baseline
`bc6d2e1b45f521fb962790a19370672bbc7ff687`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | The frozen final test ran against the unchanged baseline implementation: setup and cleanup passed, and the old Index cell failed exactly once because `Qt::AccessibleTextRole` was not a `QString` under `/private/tmp/embed-labs-coe-cell-a11y-failure-frozen.Ija4nk` |
| Five standard columns | Every valid hierarchical Index, Name, Flags, Value, and Unit cell returns an actual `QString` through `Qt::AccessibleTextRole`, exactly matching the complete Display value |
| Empty display value | Empty Unit remains a valid empty `QString`, not an invalid variant |
| Description and tooltip | Both contain object address, column heading, complete current value or an explicit empty marker, data type, Mock/offline source, and matching operation guidance |
| Truthful transport boundary | Every description states that access flags are a local engineering prototype and no controller connection or SDO transfer occurs |
| Long-cell recovery | A unique temporary ESI fixture preserves a 256-byte value and a long Chinese/Japanese/Unicode name containing literal `%1`, `%2`, `%5`, and `%%` in accessible text, description, and tooltip |
| Configured Mock edit | Only the Value cell advertises temporary local editing; selection, flags, Add to Startup availability, and the complete Project snapshot remain unchanged |
| Dynamic standard roles | A successful edit publishes Display, Edit, AccessibleText, AccessibleDescription, Tooltip, and the existing private raw value role through `dataChanged` |
| Offline reset | A fresh index resolves after reset, the original ESI value returns, the description reports offline/read-only, the edit flag is absent, and Project remains unchanged |
| Focused normal-scale test | 3 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-coe-cell-a11y-focused-frozen-normal.EH7ZAp` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-coe-cell-a11y-focused-frozen-2x.P5UqHe` |
| Complete EtherCATWorkbench normal scale | 49 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-coe-cell-a11y-workbench-final-normal.p7cwQT` |
| Complete EtherCATWorkbench 2x scale | 49 passed, 0 failed; target exit 0 under `/private/tmp/embed-labs-coe-cell-a11y-workbench-final-2x.vs1Nst` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 49, Scan 7, Diagnostics 7; 100 passed, 0 failed with target exit 0 in isolated LLDB-supervised processes under `/private/tmp/embed-labs-coe-cell-a11y-six-suites-final.cmbhBS` |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611`; exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | After PID detection, PID 98426 stayed alive for 36 seconds under `/private/tmp/embed-labs-coe-cell-a11y-product-lifecycle-final.I01hmv/enabled`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | After PID detection, PID 443 stayed alive for 37 seconds with `-noload EtherCATWorkbench` under the matching `disabled` directory; intentional SIGTERM produced target status 15 |
| Process and crash-report cleanup | No residual Embed Labs/LLDB process, no new DiagnosticReports file, and no matching ReportCrash/CrashReporter unified-log event |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| Assistive-technology claim | Standard Qt item-model metadata verified; no manual VoiceOver reading is claimed |
| Repository Device CoE boundary | Not qualified by this accessibility issue; subsequently passed under the independent `ISSUE-WB-COE-REPOSITORY-READONLY-001` qualification below |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, custom model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, SDO/controller transport, online state, or hardware access | Not performed or added; CoE remains a local Mock/offline object dictionary |

Qt's standard roles and role-specific change notification are documented at
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qabstractitemmodel.html#dataChanged>. Beckhoff's
five-column CoE Online object dictionary remains only the product comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.

## EtherCATWorkbench repository Device CoE read-only qualification

`ISSUE-WB-COE-REPOSITORY-READONLY-001` uses local baseline
`3f4ea273a559506af663ae1edbb5233d31688565`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Setup and cleanup passed; the unchanged old implementation failed exactly once because repository object `6060:00` still advertised `Qt::ItemIsEditable` under `/private/tmp/embed-labs-coe-repository-failure-final.VgDNIm`; target status 1 |
| Repository object capability | Passed; the ESI-derived Flags cell still displays `RW`, so repository presentation does not falsify device capability metadata |
| Repository model permission | Passed through the view's actual proxy model; Value flags omit `ItemIsEditable` and direct `setData(..., Qt::EditRole)` returns false |
| Rejected-edit stability | Passed; Edit, Display, AccessibleText, AccessibleDescription, and Tooltip data remain unchanged after rejection |
| Truthful operation guidance | Passed; description and tooltip report read-only, retain Mock/controller/SDO boundaries, and do not advertise temporary editing |
| Add to Startup | Passed; disabled for the repository Device and restored only for the selected writable object in the configured-slave context |
| Context reset and stale-index boundary | Passed for configured / repository / configured / repository switching, with fresh indexes resolved after every reset and no edit permission leakage |
| Configured-slave regression | Passed; the same ESI object's Mock Value remains temporarily editable in an existing configured-slave context |
| Project mutation boundary | Passed; the complete Project snapshot remains unchanged through accepted transient configured edits, rejected repository edits, selection, and context switching |
| Focused normal-scale test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-coe-repository-focused-final.3aAeuW/normal` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-coe-repository-focused-final.3aAeuW/2x` |
| Complete EtherCATWorkbench normal scale | 50 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-coe-repository-workbench-final.2YRthk/normal` |
| Complete EtherCATWorkbench 2x scale | 50 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-coe-repository-workbench-final.2YRthk/2x` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 50, Scan 7, Diagnostics 7; 101 passed, 0 failed with target status 0 in isolated LLDB-supervised processes under `/private/tmp/embed-labs-coe-repository-six-suites-final.jBXs08` |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611`; exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | PID 95408 remained running for 36.006 seconds after observation under `/private/tmp/embed-labs-coe-repository-product-enabled-final2.DrnDXO`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 96495 remained running for 36.005 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-coe-repository-product-disabled-final2.DKy1uv`; intentional SIGTERM produced target status 15; one non-fatal shared-memory message did not interrupt startup |
| Process and crash-report cleanup | No residual Embed Labs/LLDB process, new DiagnosticReports file, or matching ReportCrash/CrashReporter unified-log event |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| Visual/manual desktop inspection | Not run by design; the issue changes no geometry and all executable qualification was offscreen |
| `WITH_TESTS=ON` all-target build | Not rerun; the known unrelated EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, custom model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, SDO/controller transport, online state, or hardware access | Not performed or added; CoE remains a local Mock/offline object dictionary |

Qt defines `ItemIsEditable` as an editable item-model capability and requires
an editable model to align that flag with `setData()`:
<https://doc.qt.io/qt-6/qt.html#ItemFlag-enum> and
<https://doc.qt.io/qt-6/qabstractitemmodel.html>. Beckhoff distinguishes its
online object operation from offline device-description values while retaining
the object's `RW`/`RO` metadata:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.

## EtherCATWorkbench ESI selection cell accessibility qualification

`ISSUE-WB-ESI-SELECTION-CELL-A11Y-001` uses local baseline
`2ffc91065b6d940bd9bc6197eb96d0471dfc4795`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Setup and cleanup passed; the unchanged implementation returned an invalid `AccessibleTextRole` instead of a `QString`, so the target exited with status 1 under `/private/tmp/embed-labs-esi-selection-cell-a11y-failure.fHPgX3` |
| Standard item roles | Every Device, Type, Vendor ID, Product Code, Revision, Group, and Support cell returns a `QString` through `AccessibleTextRole` equal to its complete current Display value |
| Complete contextual recovery | `AccessibleDescriptionRole` and `ToolTipRole` contain the translated column heading, complete value, qualification, append operation, and offline/controller/network boundary |
| Supported operation | Every Supported cell states that the device can be appended to the selected offline EtherCAT Master without accessing a controller or network |
| Limited operation | Every Limited cell states that the device cannot be appended until unsupported structures are resolved; selecting the row keeps Add disabled |
| Long and literal text | Complete 256-character Chinese/Unicode Device, Type, and Group values plus literal `%1`, `%2`, and `%%` text are preserved exactly |
| Existing dialog behavior | Highest-revision filtering, search, sorting, extended columns, selection, icons, identity roles, Add gating, insertion, and geometry remain unchanged |
| Focused normal-scale test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-esi-selection-cell-a11y-pass-1.R4JfjQ` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-esi-selection-cell-a11y-focused-2x.sgxlpQ` |
| Complete EtherCATWorkbench normal scale | 51 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-workbench-full-1.pA3kEO` |
| Complete EtherCATWorkbench 2x scale | 51 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-esi-selection-cell-a11y-full-2x.wgcUmB` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 51, Scan 7, Diagnostics 7; 102 passed, 0 failed with target status 0 in isolated LLDB-supervised processes under `/private/tmp/embed-labs-six-suite.2xoXwk` |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611`; exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | PID 50789 remained running for 36.011 seconds after observation under `/private/tmp/embed-labs-esi-selection-cell-a11y-lifecycle.2erfTD/enabled`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 51907 remained running for 36.002 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-esi-selection-cell-a11y-lifecycle.2erfTD/disabled`; intentional SIGTERM produced target status 15 |
| Process and crash-report cleanup | No residual Embed Labs/LLDB process, new DiagnosticReports file, or matching ReportCrash/CrashReporter unified-log event |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| Assistive-technology claim | Standard Qt widget/item-model metadata verified; no manual VoiceOver reading is claimed |
| Visual/manual desktop inspection | Not run by design; the issue changes no geometry and all executable qualification was offscreen |
| `WITH_TESTS=ON` all-target build | Not rerun; the known unrelated EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, custom model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, or hardware access | Not performed or added; selection remains an offline ESI/Project operation |

Qt's standard item accessibility interfaces are documented at
<https://doc.qt.io/qt-6/qstandarditem.html> and
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum>. Beckhoff's offline device
selection, catalogue search, extended information, and revision comparison is
documented at
<https://infosys.beckhoff.com/content/1033/el331x/1036999947.html> and
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2477595531.html>.

## EtherCATWorkbench project-scoped Details draft qualification

`ISSUE-WB-DETAILS-UNRELATED-PROJECT-DRAFT-001` uses local baseline
`e5ded42f8dd3d3d861de830e3408de12f1847334`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | The final frozen test ran against the unchanged implementation; setup and cleanup passed, and Project B's rename replaced Project A's focused Unicode `%1` draft with Project A's persisted name under `/private/tmp/embed-labs-details-draft-failure-final.4j1lxv`; target status 1 |
| Refresh ownership | `ProjectService::projectChanged` refreshes Details only when the changed snapshot ID equals the current `PropertyPageContext::projectId` |
| Current-context evaluation | The callback reads `m_context.projectId` when the signal arrives; it does not retain the Project selected when the connection was created |
| Unrelated Project continuity | Project A draft text, `isModified()`, editor focus, selection, Details context/title, and persisted snapshot remain unchanged after a real Project B rename |
| Current Project continuity | A real Project A rename still refreshes the same General editor and Details title to the persisted name and clears the modified flag |
| Repository context | A null Project ID ignores Project changes; repository ownership and device refresh signals are unchanged |
| Shared page boundary | The filter is at the common Details refresh gate, before General, EtherCAT, Process Data, CoE, Startup, DC, Online, Diagnostics, or dynamic Provider page updates |
| Same-Project conflict policy | Intentionally unchanged; a current Project change remains authoritative and may replace an in-progress page draft. Dirty/conflict merging is outside this issue |
| Focused normal-scale test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-details-draft-focused-frozen-final.Y5vcy2/normal` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-details-draft-focused-frozen-final.Y5vcy2/2x` |
| Complete EtherCATWorkbench normal scale | 52 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-details-draft-workbench-final.ygJUZO/normal` |
| Complete EtherCATWorkbench 2x scale | 52 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-details-draft-workbench-final.ygJUZO/2x` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 52, Scan 7, Diagnostics 7; 103 passed, 0 failed with target status 0 in isolated LLDB-supervised processes under `/private/tmp/embed-labs-details-draft-six-suites-final.MG71MB` |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611`; exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | PID 1913 remained running for 36.010 seconds after observation under `/private/tmp/embed-labs-details-draft-lifecycle-final.Zu4F0t/enabled`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 2968 remained running for 36.011 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-details-draft-lifecycle-final.Zu4F0t/disabled`; intentional SIGTERM produced target status 15 |
| Process and crash-report cleanup | No residual Embed Labs/LLDB process, new DiagnosticReports file, or matching ReportCrash/CrashReporter unified-log event |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| Visual/manual desktop inspection | Not run by design; the issue changes no geometry and all executable qualification was offscreen |
| `WITH_TESTS=ON` all-target build | Not rerun; the known unrelated EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, or hardware access | Not performed or added; Details remains a local Mock/offline Project presentation |

Qt documents the modified and `setText()` reset behavior at
<https://doc.qt.io/qt-6/qlineedit.html#modified-prop> and typed functor
connections at <https://doc.qt.io/qt-6/qobject.html#connect-5>. Qt Creator
20.0's Project settings keep listeners on their owning Project items:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectwindow.cpp#L580-L619>.
Beckhoff's TwinCAT comparison scopes the available configuration tabs to the
terminal selected in Solution Explorer:
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html>.

## EtherCATWorkbench General property tree accessibility qualification

`ISSUE-WB-GENERAL-PROPERTY-TREE-A11Y-001` uses local baseline
`2b956da48104c97414da33190b9772e033969ee7`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Setup and cleanup passed; the unchanged implementation failed exactly because the General property tree accessible name was empty under `/private/tmp/embed-labs-general-property-a11y-failure-frozen.0HPnE7`; target status 1 |
| Widget metadata | The visible Property/Value tree has a concise accessible name and a contextual description identifying read-only offline data and the controller/network/hardware boundary |
| Standard item roles | Every Property and Value cell returns an actual `QString` through `AccessibleTextRole` equal to its complete current Display value |
| Complete contextual recovery | `AccessibleDescriptionRole` and `ToolTipRole` contain the column heading, property name, complete value, read-only/offline status, and controller/network/physical-hardware boundary |
| Real contexts | Passed through the actual configured-slave General page and its model-generated Modules / Channels General page |
| Long and literal text | Complete long Unicode ESI match, source path, and owner-slave values plus literal `%1`, `%2`, and `%%` text are preserved exactly |
| Existing presentation | Row order, headers, selection, resize modes, elision, page geometry, Project data, and context ownership remain unchanged |
| Focused normal-scale test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-general-property-a11y-focused-final.E1aBys/normal` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-general-property-a11y-focused-final.E1aBys/2x` |
| Offscreen render inspection | Normal 1100 by 720, SHA-256 `b4a03964e53a857323e44dc18a8614260a607c215a3509b8aa057011ebb731d0`; 2x 2200 by 1440, SHA-256 `661d4ac799a3617fc94d8c8c0b3319d2d5731f486b0839628321efc447090afe`; no overlap or scale drift, with intentional visual elision recoverable through metadata |
| Complete EtherCATWorkbench normal scale | 53 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-general-property-a11y-workbench-final.D2MHZA/normal` |
| Complete EtherCATWorkbench 2x scale | 53 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-general-property-a11y-workbench-final.D2MHZA/2x` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 53, Scan 7, Diagnostics 7; 104 passed, 0 failed with target status 0 in isolated LLDB-supervised processes under `/private/tmp/embed-labs-general-property-a11y-six-suites-final.JfEsWm` |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611`; exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | PID 78919 remained running for 36.005 seconds under `/private/tmp/embed-labs-general-property-a11y-lifecycle-final.WKCTbC/enabled`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 78918 remained running for 36.006 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-general-property-a11y-lifecycle-final.WKCTbC/disabled`; intentional SIGTERM produced target status 15 |
| Process and crash-report cleanup | No residual Embed Labs/LLDB process, new DiagnosticReports file, or matching ReportCrash/CrashReporter unified-log event |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| Assistive-technology claim | Standard Qt widget/item-model metadata verified; no manual VoiceOver reading is claimed |
| `WITH_TESTS=ON` all-target build | Not rerun; the known unrelated EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, custom model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, or hardware access | Not performed or added; General remains local read-only offline Project presentation |

Qt's widget accessibility properties and standard item data roles are
documented at <https://doc.qt.io/qt-6/qwidget.html#accessibleName-prop> and
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum>. Beckhoff's selected-terminal
General tab provides the information-hierarchy comparison:
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10832178955.html>.

## Verification states

Use only these evidence labels:

- `Verified`: reproduced in the current local checkout or executable.
- `Installed`: tool exists but the required product workflow was not run.
- `Existing artifact`: an older build runs; current-source build not proven.
- `Pending`: required but not yet executed.
- `Deferred`: intentionally outside the current phase or blocked by policy.
- `Blocked`: attempted, but an identified external prerequisite prevents the
  workflow from completing on the current host.

Do not convert `Installed`, `Existing artifact`, `Pending`, `Blocked`, or
`Deferred` into `Verified` without the matching build or runtime evidence.
