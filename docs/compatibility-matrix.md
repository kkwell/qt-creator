# Local Compatibility Matrix

## Baseline matrix

| Item | Supported or observed baseline | Evidence status |
|---|---|---|
| Product branch | `embed-labs` only | Verified |
| Issue baseline commit | `9fd3c844c3f61557323a1a4f57e668e931c8de09` | Verified |
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
details routing. Real modular-profile data, additional UI coverage, and the
user-policy-deferred upstream rehearsal remain open.

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
| ESI repository | Devices storage/parser/provider and Workbench import/reload/cancel UI verified; local/offline only |
| Individual ESI catalogue-device General page | Verified with TwinCAT-aligned identity, configuration coverage, qualification/source details, explicit unavailable state, and no controller access |
| Master-side ESI device insertion | Verified with context-only `Add New Item...`, ESI search, latest/previous revision handling, qualification gating, stable IDs, and Project Undo/Redo |
| Supported ESI device drag-and-drop | Verified for private stable-ID CopyAction, exact active-Master targeting, append semantics, rejection boundaries, and Project Undo/Redo |
| Scan UI and topology comparison | Stage 5 verified with Mock provider only |
| WKC/DC/link diagnostics | Stage 6 verified with Mock provider only |
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
| Focused EtherCATWorkbench plugin tests | 31 passed, 0 failed |
| Six-plugin EtherCAT regression | 82 passed, 0 failed in isolated processes |
| Failure-first tree contract test | Failed to compile on missing source-ID routing before implementation, as expected |
| Failure-first navigation layout test | Failed on `ElideRight`, then on missing accessible metadata, before both fixes |
| Failure-first navigation keyboard test | Compiled and failed because the navigation container focus proxy was null, as expected |
| Focused navigation keyboard test | 3 passed, 0 failed at normal scale |
| Focused navigation keyboard test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Failure-first CoE Online page test | Compiled and failed on the missing `CoE Online` page descriptor before implementation, as expected |
| Failure-first unified-status test | Compiled and failed because no Workbench status-bar control was registered, as expected |
| Failure-first command-strip test | Compiled and failed because EtherCAT Mode had no engineering command strip, as expected |
| Failure-first provider-state tree test | Failed to compile on missing difference/issue/diagnostics navigation APIs before implementation, as expected |
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
| CoE offline, repository, and missing-ESI read-only boundaries | Passed |
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
| Offline slave remove/reorder workflow | Passed with normalized positions, boundary enablement, stable selection, selection repair, Undo, and Redo |
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
| macOS accessibility lock-transition observation | One Qt 6.11 accessibility crash was captured during a lock transition; a second RxPDO-selection run remained alive until the Mac locked, so the event is not reproduced and remains a qualification risk |
| Dynamic property-page provider removal | Passed |
| Dynamic Scan/Diagnostics availability and removal | Passed |
| Workbench test-process cleanup | Focused normal/2x and full-suite processes exited 0 after widget, controller, selection, and Provider cleanup; this issue adds no thread, timer, future, or Provider |
| Scan snapshot overlay | Passed for exact, Missing, Added, Revision, Vendor, source label, full-detail search, and aggregate count/severity |
| Diagnostics snapshot overlay | Passed for Run/OP, SAFEOP/error, AL detail, missing snapshot, alarm/error marker, stopped state, and cleanup |
| Difference/issue/Diagnostics ActionManager navigation | Passed with stable selection, filter clearing, ancestor expansion, and shared QAction registration |
| Tree context and navigation ActionManager identity | Passed for seven navigation and five offline-topology commands, shared Expand/Collapse buttons, context-only strip exclusion, enabled state, stable-ID copy, and placeholder protection |
| EtherCATCore regression tests | 17 passed, 0 failed |
| EtherCATProject regression tests | 12 passed, 0 failed |
| EtherCATDevices regression tests | 8 passed, 0 failed |
| EtherCATScan regression tests | 7 passed, 0 failed |
| EtherCATDiagnostics regression tests | 7 passed, 0 failed |
| Product version inventory | All 16 allow-listed plugins present and recognized |
| Normal Release product build | Passed with 16-plugin allow-list |
| Enabled GUI startup | Passed with clean temporary settings; Workbench initialized and delayed-initialized, then remained stable until the intentional 10-second timeout |
| Explicitly disabled startup | Passed with `-noload EtherCATWorkbench` and clean temporary settings; Workbench, Scan, and Diagnostics were absent while Core, Devices, and Project initialized, then the product remained stable until the intentional 10-second timeout |
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
