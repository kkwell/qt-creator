# Local Compatibility Matrix

## Baseline matrix

| Item | Supported or observed baseline | Evidence status |
|---|---|---|
| Product branch | `embed-labs` only | Verified |
| Current issue baseline commit | `1ba2305e0701387fd12e34b07f895d8218108c13` | Verified |
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
| EtherCATWorkbench | EtherCAT mode, device tree, selection, master-side ESI insertion, supported-device drag-and-drop, offline property pages, embedded commissioning, and native quick controller controls | Current English Workbench suite passed 95 tests with no failures; controller prompts use one passive Application Output channel, the status bar excludes controller connection state, and the lower-left controls retain the compact actionable projection |
| EtherCATScan | Local Mock scan, topology comparison, and checked acceptance | Stage 5 verified |
| EtherCATDiagnostics | Local Mock state, WKC, DC, alarm, and performance views | Stage 6 verified |
| EtherCATProductApi | Headless Embed Labs Product API v1.10 client with bounded v1.9 compatibility | Current English ProductApi suite passed 71 tests with no failures and 1 hardware skip; endpoint parsing/configuration is included, generic Start and explicit DC hardware lifecycles each passed 3 tests with no failures, and no FreeRun lifecycle ran |

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

### Online EtherCAT profile

The online profile is incremental and does not replace qualified offline/Mock
behavior. Its ordered gates are:

1. semantic controller connection Core API;
2. multi-vendor Provider/Profile selection contract;
3. headless Embed Labs ProductApi adapter;
4. provider-neutral embedded Communication page;
5. controlled real discovery;
6. Project Configuration versus Current Bus comparison and Apply;
7. truthful embedded topology; and
8. real diagnostics.

The historical Gate 4 record includes focused normal- and 2x-scale tests,
complete Workbench and isolated seven-suite runs, product/plugin/translation
gates, and the dated real Qt Connect/Refresh/Disconnect flow. The synchronized
product profile includes the headless `EtherCATProductApi`. Its codec and local
loopback checks remain non-hardware protocol evidence, while the 2026-07-24 UI
flow is separate real-controller evidence. The dated record also includes the
status-bar correction and a second Handshaking, Connected, and
Disconnect-to-Offline hardware UI pass. Those dated UI gates remain historical.
The earlier single-instance, full-path product observation showed Workbench,
Simplified Chinese, hidden production Mock UI, and the compact tree. The
current endpoint and unified-output revision is covered by widget-level
Workbench tests and a three-pass ProjectExplorer passive-output lifecycle test.
No controller connection or hardware command was executed in this round.

The later local control extension implements leased discovery and the Product
API v1.10 explicit timing-mode start contract. The 2026-07-24 record observed
v1.10 with feature bits `0xfff` and reached AcquireControl, Configuration,
three-slave DiscoverTopology, ReleaseControl, and safe cleanup. Exact restore
of `A/11/810` returned `CAPABILITY_MISMATCH (-20)`, so that run did not qualify
runtime transitions. The same record observed a reboot return to persistent
release24/v1.9.

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
| Editable DC page | Verified, including keyboard-accessible read-only preview of every parsed ESI mode on repository Device pages and unchanged Project-owned editing on configured slaves |
| Offline EtherCAT project | Stage 2 verified |
| ESI repository | Devices storage/parser/provider, Workbench import/reload/cancel UI, and actionable empty guidance verified; local/offline only |
| Individual ESI catalogue-device General page | Verified with TwinCAT-aligned identity, configuration coverage, qualification/source details, explicit unavailable state, and no controller access |
| Master-side ESI device insertion | Verified with context-only `Add New Item...`, ESI search, latest/previous revision handling, qualification gating, stable IDs, and Project Undo/Redo |
| Supported ESI device drag-and-drop | Verified for private stable-ID CopyAction, exact active-Master targeting, append semantics, rejection boundaries, and Project Undo/Redo |
| Scan UI and topology comparison | Stage 5 verified with Mock provider only |
| WKC/DC/link diagnostics | Stage 6 verified with Mock provider only |
| Optional Scan/Diagnostics Provider state | Verified for absent, registered/unavailable, and available states with public `Local Mock` producer names and no installation inference |
| Product API v1 | v1.10 explicit timing-mode client contract is locally qualified with bounded v1.9 compatibility; RAM-only v1.10 hardware is qualified through Scan, with Restore and runtime transitions pending |
| Controller connection Core contract | Verified multi-provider/profile semantic prerequisite with arbitrary named channels; no Qt network or hardware claim |
| Provider-neutral Communication page | Embedded Master Details page for connection, automatic/manual Acquire, Configuration, Scan, Restore/Release, Actual Bus, and information; runtime uses the lower-left native quick controls; earlier read-only qualification remains historical |
| Real EtherCAT scan | Typed leased discovery is verified against three real slaves and remains separate from the offline Project; Project Apply remains pending |
| Real controller diagnostics | Existing Mock contract reusable; ProductApi Push/Bulk source pending |
| Physical topology graph | Linear scan order only with current API; branch/star graph blocked by missing port-neighbor edge ABI |
| ECPKG/ECFG/ETIR | Headless transfer/validate/optional-activate accepts only already-built immutable ECPKG bytes; construction, signing, Workbench invocation, ECFG/DC editing, and ETIR generation remain out of scope |
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
| CoE Mock raw-value edit and visible input rejection | Passed through a real inline editor for normalized-empty, incomplete-byte, illegal-hex, and applicable fixed-width cases; accepted Mock bytes and Project state remain unchanged on rejection |
| CoE Add to Startup cancel, confirm, append-only, and Undo | Passed |
| CoE offline, missing-ESI, and repository read-only boundaries | Passed; repository `RW` capability metadata remains visible while its Mock Value edit flag and direct model mutation are rejected by the separate current qualification below |
| CoE focused test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed |
| Shared StateService Offline/Busy/Error priority and Mock labels | Passed |
| Status tooltip, drop-down details, Mode visibility, and cleanup ownership | Passed |
| Status icon style metric and content-width guard | Passed |
| Focused status test at `QT_SCALE_FACTOR=2` | 3 passed, 0 failed after fixing the reproduced width compression |
| TwinCAT-inspired ordered Startup list and action layout | Passed in widget/model flow test |
| Startup ESI proposal, explicit Store/Restore, and populated repository catalogue read-only cells | Passed |
| Startup New/Edit/Delete, enable, Move Up/Down, and fixed request constraints | Passed |
| Startup type/raw-value/order rejection and Undo/Redo | Passed |
| Configured-slave Startup manual no-ESI empty state | Passed |
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
| Bounded right-elided tree columns in a narrow navigation area | Passed in normal/2x widget tests; complete values remain in tooltip, accessibility, Filter, and Find roles |
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

## EtherCATWorkbench CoE non-conflicting inline-draft qualification

`ISSUE-WB-COE-NONCONFLICTING-INLINE-DRAFT-REFRESH-001` is qualified from
local baseline `d26f695ec7581b7864e87577d0b935997a30546d`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the new Workbench test changed; production `coeonlinepage.cpp/.h` retained SHA-256 `4f6a83237a8be1158799242f5e26f31490ee7559349c1aabc9fddc0d95c5fe0d` / `ef599680bc35e50556b8f222b06eb7b2672dfac01324ef5993e0a857d5ddb66a` and blobs `c758168bf1fdc7a02634e22546b14d5c54978ae3` / `83d07f6d006009bebdf70b2a087d8ce7b2231a63`; a real Project rename emitted two proxy model resets instead of zero, target status 1 |
| Stable preservation gate | Same non-null Project, configured-slave node and kind, editable Mock source, scalar address, writable non-synthetic object, offline/final-Mock bytes, parsed/raw type, process-data role, edited-state provenance, active-root child count, and continued visibility through the current proxy filter |
| Editor state | The same real `QLineEdit`, uncommitted text, modified state, focus, selection, cursor, and Undo availability survive eligible refreshes; native IME composition is not claimed |
| Project rename | A real rename preserved the editor without model reset; the underlying `EditRole` remained `08`, proving the draft was not silently accepted |
| Repository metadata | Fresh active `6060:00` Name and sibling metadata rendered; source and proxy `dataChanged()` covered the active Name but excluded its Value while the same editor and underlying Mock value remained unchanged |
| Structural synchronization | Inserting `605F:00` before active `6060:00` moved its persistent proxy index forward one row; removing the sibling moved it back. Both source and proxy remained under `QAbstractItemModelTester`, with row signals and no reset |
| Structural coverage limit | The test directly qualifies top-level sibling insertion/removal. The same private recursive path handles children under source/proxy model testers, but child insertion/removal and the defensive move branch are not directly exercised or claimed as separate user-facing behavior |
| Accepted override plus draft | After Return accepted local Mock `5A`, a new uncommitted `6B` survived another metadata refresh; `6072:00` rendered `refreshed over accepted override` through source and proxy `dataChanged()`, while the underlying active value stayed `5A` |
| Delegate semantics | Escape discarded the preserved draft; Return accepted a valid one-byte value only in the page-local Mock model. Neither changed the Project snapshot. Rejected-Return presentation is qualified separately by `ISSUE-WB-COE-MOCK-EDIT-REJECTION-FEEDBACK-001` |
| Explicit clear boundaries | Update List discarded an active draft and regenerated `09`; Show Offline discarded a draft, displayed authoritative `08`, and returned to unedited Mock `09` |
| Authority conflict | A width/value change closed the editor and rendered generated `0A00`; a fixed/read-only change closed it, removed editability, and rendered `08` |
| Object removal and context | Removing `6060:00` destroyed its editor; a Project-node switch destroyed the CoE page and editor. Drafts never changed the Project snapshot |
| Final source identity | SHA-256: `coeonlinepage.cpp` `7f0c8d977575d87f7ad08c6a94fcd0708f03c2d59f5e6891aa719828c4688d15`, `.h` `8efa640a8cf8fad46d396dd22269c7c1a5ea68a448179cd67745e6cb1fc433e2`, tests `.cpp` `44c6ed51ddcac07104b39670ff8e51fbb1ffb594556bac332cbab04be2d1c5a2`, tests `.h` `69232684a3c22e18cb3f23f21ae2ab0e334fea906e766dffd8a3377252ce3a20`; blobs `8351fff476b0edfed51a09f11c9185dfe81f9c7c`, `ee362007d7152e33df3eb8e10f9655a563ab200f`, `8fde78bcdae8caa914cfe254c7f97ceed1e0089c`, `b30eedf2b177de7b11b0c2ccc1aaa2f0a8ab7ba8` |
| Focused final regression | Normal and 2x runs each passed init, `testCoeInlineDraftSurvivesNonConflictingRefresh`, and cleanup: three pass events, zero failures, target status 0 |
| Complete Workbench regression | Two normal and two 2x complete runs each passed 78 events with target status 0: 312 pass events, zero failures |
| Six-plugin isolation | Core 17, Project 12, Devices 8, Workbench 78, Scan 7, and Diagnostics 7: 129 pass events, zero failures |
| Product build and identity | The test Workbench target and complete `WITH_TESTS=OFF` product build passed; the product contains 16 plugin dylibs. Product executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product/test Workbench dylibs `306c42e407646c1a3841f1fafb1407bcf002006c400117d9ef300b41532f56b1` / `d38fc759ac9b20d8c066b147947c00726d0b04d06acf682f392186af827c50eb` |
| Main-program lifecycle | Enabled: 37/37 alive and 37/37 Workbench loaded. Explicit `-noload EtherCATWorkbench`: 37/37 alive and 0/37 loaded. Passed-through SIGTERM produced target status 15 for both, with zero residual processes, new Embed Labs DiagnosticReports, or matching crash-service events |
| Invisible execution policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or manual UI inspection |
| Superseded test attempts | One preliminary run reused non-distinct Repository metadata and incorrectly expected one updated device; the fixture was made unique. Two post-review invocations used an invalid dot-form selector, printed usage, and exited 255 before testing. Two subsequent runs used an over-strict full-string expectation that omitted the existing bilingual Name prefix; both status-1 logs contained the refreshed marker. The assertion was corrected to check that unique marker, and final comma-form 1x/2x runs passed. All five logs are retained and excluded from green qualification |
| Superseded build attempts | The first failure-first build used the unavailable `QPersistentModelIndex::siblingAtColumn()` test helper; the test was corrected to index through its model before the valid red run. A later intermediate test-only edit placed read-only fixture declarations in the wrong test, producing duplicate and undeclared identifiers; the declarations were moved into the target test. Both compiler logs are retained, excluded from qualification, and superseded by the passing test/product builds |
| Known non-fatal messages | Each final complete Workbench log contains the existing ProjectExplorer TaskHub category soft assertion while `testInvalidProjectPresentationAndLifecycle` still passes; the disabled product run contains one shared-memory initialization message while all 37 samples remain alive, shutdown returns expected status 15, and no crash artifact appears |
| Deliberate exclusions | Multiple or persistent editors, generic tree/draft synchronization, cross-node cache, native IME composition, persistence, Project/Repository revision, CAS/merge/conflict UI, public API/model role, network, ADS, online CoE/SDO, controller, PLC, and hardware behavior are not claimed |
| qbs execution | Not run; no CMake or qbs description changed |

Evidence is under
`/private/tmp/embed-labs-wb-coe-inline-draft-001.177q7M`. Qt's reset,
delegate, persistent-index, and line-edit contracts are at
<https://doc.qt.io/qt-6/qabstractitemmodel.html#beginResetModel>,
<https://doc.qt.io/qt-6/qabstractitemview.html#reset>,
<https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData>,
<https://doc.qt.io/qt-6/qpersistentmodelindex.html>, and
<https://doc.qt.io/qt-6.8/qlineedit.html>. Beckhoff's CoE page is referenced
only for object-value, RW/RO, Offline-value, and Update List terminology at
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

## EtherCATWorkbench EtherCAT SyncManager cell accessibility qualification

`ISSUE-WB-ETHERCAT-SYNCMANAGER-CELL-A11Y-001` uses local baseline
`5cd0bade8355565a07969d97b49965bce93503b2`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Setup and cleanup passed; the unchanged implementation returned no `QString` through `AccessibleTextRole` for configured-slave row 0 / SM, so the target exited with status 1 under `/private/tmp/embed-labs-syncmanager-a11y-failure-final.eumVVB` |
| Widget metadata | Configured-slave and repository Device views identify their table as read-only offline ESI defaults with no controller, network, or physical-hardware access |
| Standard item roles | Every SM, Name, Direction, Address, Size, Control, and Enabled cell returns an actual `QString` through `AccessibleTextRole` equal to its complete current Display value |
| Complete contextual recovery | `AccessibleDescriptionRole` and `ToolTipRole` are equal and contain the column heading, SM index/name, complete value, ESI source, and read-only/offline/controller/network/physical-hardware boundary |
| Real contexts and lifecycle | The same actual property page passes ConfiguredSlave to Device to restored ConfiguredSlave context switching without stale values or roles |
| Long, literal, and empty text | A complete long Chinese/Japanese/Unicode Outputs name plus literal `%1`, `%2`, and `%%` is preserved exactly; a legal empty SM name remains an empty `QString` while its description states `unnamed` and `Empty`; both rows remain non-editable |
| Existing presentation | Seven headers, two rows, direction/address/size/control/enabled truth, resize modes, horizontal scrolling, and Project/ESI behavior remain unchanged |
| Focused normal-scale test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-syncmanager-a11y-focused-final.cR27sE/normal` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-syncmanager-a11y-focused-final.cR27sE/2x` |
| Offscreen render inspection | Normal 1100 by 720, SHA-256 `24f70cf9b5fcbaf3d9757b495f553e12ce6b638fd64776ff59af5279c1ba78ba`; 2x 2200 by 1440, SHA-256 `9247cb4e3560c385976f94077a47772182d1520c1fe3e1519ceefb8fc3c519a0`; no overlap or scale drift, with the deliberate long extent reachable through the existing horizontal scroll bar and recoverable through metadata |
| Complete EtherCATWorkbench normal scale | 54 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-syncmanager-a11y-workbench-final.RVRsLZ/normal` |
| Complete EtherCATWorkbench 2x scale | 54 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-syncmanager-a11y-workbench-final.RVRsLZ/2x` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 54, Scan 7, Diagnostics 7; 105 passed, 0 failed with target status 0 in isolated LLDB-supervised processes under `/private/tmp/embed-labs-syncmanager-a11y-six-suites-final.k5a0Wn` |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611`; exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | PID 86456 remained running for 36.004 seconds under `/private/tmp/embed-labs-syncmanager-a11y-lifecycle-final.yshQnN/enabled`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 88370 remained running for 36.001 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-syncmanager-a11y-lifecycle-final.yshQnN/disabled`; intentional SIGTERM produced target status 15; one non-fatal shared-memory initialization message did not interrupt startup and was not a crash |
| Process and crash-report cleanup | No residual Embed Labs/LLDB process, new DiagnosticReports file, or matching ReportCrash/CrashReporter unified-log event |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| Assistive-technology claim | Standard Qt widget/item metadata verified; no manual VoiceOver reading is claimed |
| `WITH_TESTS=ON` all-target build | Not rerun; the known unrelated EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, custom model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, or hardware access | Not performed or added; the EtherCAT SyncManager table remains local read-only offline ESI presentation |

Qt's standard item roles and `QTreeWidgetItem` role storage are documented at
<https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qtreewidgetitem.html#setData>. Beckhoff's EtherCAT tab
and FMMU/SM reference provide only the selected-device hierarchy and field
semantics comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1342524811.html> and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/4981170059.html>.

## EtherCATWorkbench topology cell accessibility qualification

`ISSUE-WB-TOPOLOGY-CELL-A11Y-001` uses local baseline
`23d67736335450d4fce299434f0e13024758cf2f`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Initialization and cleanup passed; the unchanged implementation failed exactly because row 0 / Position returned no `QString` through `AccessibleTextRole` under `/private/tmp/embed-labs-topology-cell-a11y-failure.z1vqwP`; target status 1 |
| Table widget metadata | The stack-local table identifies read-only configured slave order and identities from the current offline Project, states that physical ports are not modeled, and excludes controller, network, and physical-hardware access |
| Ten standard columns | Every Position, Name, Auto Inc Addr, Previous, Port, Vendor, Product, Revision, Alias, and Status cell returns an actual `QString` through `AccessibleTextRole` equal to its complete Display value |
| Complete contextual recovery | `AccessibleDescriptionRole` and `ToolTipRole` are equal and contain the column heading, configured Position, complete slave Name, complete current value, and all offline/Project/controller/network/physical-port/hardware boundaries |
| Truthful topology boundary | Previous is configured-list context, Port remains `Not modeled`, and Status remains `Offline configured`; none is claimed as scanned topology, verified physical wiring, or runtime EtherCAT state |
| Long and literal text | Two complete Chinese/Japanese/Unicode names containing literal `%1`, `%2`, and `%%` are preserved; row 1 Previous exactly preserves row 0's complete long Name; both rows remain non-editable |
| Existing bounds behavior | The ten headers, Display values, slave ordering, `ResizeToContents`, `ElideNone`, horizontal scrolling, screen bounds, centering, modal stack lifetime, and Close path remain unchanged and continue to pass their separate regression |
| Focused normal-scale test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-topology-cell-a11y-focused-final.PDcyTx/normal` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-topology-cell-a11y-focused-final.PDcyTx/2x` |
| Offscreen render inspection | Normal 784 by 279, SHA-256 `e5abaf35897884abea0ed9dd9c582d7aa7fa1746f139c415209675f51c2ba014`; 2x 768 by 558, SHA-256 `fdedc08c0be16825085bcb03a3b7d962c137b9b4b3df97af2f6b29e994bef7d9`; horizontal recovery and Close remain usable without overlap or scale drift |
| Complete EtherCATWorkbench normal scale | 55 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-topology-cell-a11y-workbench-final.BFPQdu/normal` |
| Complete EtherCATWorkbench 2x scale | 55 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-topology-cell-a11y-workbench-final.BFPQdu/2x` |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 55, Scan 7, Diagnostics 7; 106 passed, 0 failed with target status 0 in isolated LLDB-supervised processes under `/private/tmp/embed-labs-topology-cell-a11y-six-suites-final.HcMvzr` |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611`; exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | PID 98032 remained running for 36 seconds after exact PID observation under `/private/tmp/embed-labs-topology-cell-a11y-product-lifecycle-final.TaMURB/enabled-evidence-1310`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 99181 remained running for 36 seconds after exact PID observation with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-topology-cell-a11y-product-lifecycle-final.TaMURB/disabled-evidence-1312`; intentional SIGTERM produced target status 15; one non-fatal shared-memory initialization message did not interrupt startup |
| Process and crash-report cleanup | No residual Embed Labs/LLDB process, new DiagnosticReports file, or matching ReportCrash/CrashReporter unified-log event after 2026-07-19 13:09:58 +0800 |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or crash dialog |
| Assistive-technology claim | Standard Qt item metadata verified; no manual VoiceOver reading is claimed |
| `WITH_TESTS=ON` all-target build | Not rerun; the known unrelated EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, custom model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, physical-port model, or hardware access | Not performed or added; Topology remains a read-only current offline Project presentation |

Qt's three standard string roles and `QTreeWidgetItem` storage are documented
at <https://doc.qt.io/qt-6/qt.html#ItemDataRole-enum> and
<https://doc.qt.io/qt-6/qtreewidgetitem.html#setData>. Beckhoff documents both
configured and online capabilities in its larger Topology product:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446515467.html> and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1277974411.html>.
The standard metadata and explicit offline exclusion are Embed Labs Qt-native
behavior.

## EtherCATWorkbench repository Device DC mode-preview qualification

`ISSUE-WB-DC-REPOSITORY-MODE-PREVIEW-001` uses local baseline
`266325f933f0072c61ccec29ec1059d7e1610a29`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | With only the new regression compiled against baseline `dcpage.cpp` blob `487c6d3e368925833d7e907d281c92c250d89fc2`, initialization and cleanup passed and the unchanged production implementation failed exactly because the repository Device mode selector was disabled; target status 1 with complete build/LLDB/status evidence under `/private/tmp/embed-labs-dc-repository-preview-final.rnvHP6/failure` |
| Real ESI context | A unique imported ESI Device supplies two real modes, `Sync0` and `Sync0 + Sync1`, through the existing Devices Provider and Details/DC page |
| Keyboard operation | The real visible DC page focuses the selector and an actual Qt Down key activates the second ESI item |
| Complete second-mode preview | Passed for AssignActivate `0x0700`, SYNC0 cycle/shift `250000 / -1000`, and SYNC1 cycle/shift `500000 / 1000` |
| Read-only boundary | The combo line editor stays read-only; Enable, AssignActivate, SYNC0/SYNC1, and potential-reference-clock controls remain disabled or read-only |
| Project isolation | Repository activation returns before `submitConfiguration()`; no Project exists, no ProjectService mutation or Undo command occurs, and no preview state is persisted |
| Context reset | Clearing and restoring the repository Device destroys the old page, recreates it from immutable ESI data, and restores the first `Sync0` mode |
| User and accessibility disclosure | Summary, localized accessible description, and tooltip identify a read-only offline preview with no Project modification or controller/network/physical-hardware access |
| Configured-slave compatibility | Existing editable configured-slave ESI selection still uses the checked ProjectService command and Undo/Redo path |
| Focused normal-scale test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-dc-repository-preview-final.rnvHP6/focused-normal` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-dc-repository-preview-final.rnvHP6/focused-2x` |
| Existing built-in Device regression | Normal and 2x each passed 3 events under the same root's `builtins-normal` and `builtins-2x` directories |
| Complete EtherCATWorkbench normal scale | 56 passed, 0 failed; target status 0 under the same root's `workbench-normal` directory |
| Complete EtherCATWorkbench 2x scale | 56 passed, 0 failed; target status 0 under the same root's `workbench-2x` directory |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 56, Scan 7, Diagnostics 7; 107 passed, 0 failed with target status 0 in isolated LLDB-supervised processes under the same root's `six-suites` directory |
| Interim harness fault | A first reset assertion retained a destroyed test-only combo pointer; LLDB caught it before system crash handling, and the final test uses `QPointer`, waits for destruction, and resolves the recreated widget; the persisted full-turn and final lifecycle audits found no diagnostic report or crash-service event |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611`; exactly the 16 allow-listed plugin dylibs are present |
| Enabled offscreen startup | PID 21400 remained running for 37 seconds under `/private/tmp/embed-labs-dc-repository-preview-final.rnvHP6/lifecycle-enabled`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 28526 remained running for 37 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-dc-repository-preview-final.rnvHP6/lifecycle-disabled`; intentional SIGTERM produced target status 15 |
| Process and crash-report cleanup | No residual Embed Labs/LLDB process, new DiagnosticReports file, or matching ReportCrash/CrashReporter/diagnosticd unified-log event after 2026-07-19 13:56:24 +0800; the persisted full-turn audit from 13:30 is also empty |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or system crash dialog |
| `WITH_TESTS=ON` all-target build | Not rerun; the known unrelated EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, or hardware access | Not performed or added; the feature is a transient local ESI presentation |

Qt's user-activation and combo model/view contracts are documented at
<https://doc.qt.io/qt-6/qcombobox.html>, and contextual widget descriptions at
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Beckhoff's
Distributed Clock page provides the operation-mode and timing comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>.
The read-only repository preview and explicit no-Project/no-controller boundary
are Embed Labs Qt-native behavior.

## EtherCATWorkbench repository Device DC empty-state qualification

`ISSUE-WB-DC-REPOSITORY-EMPTY-001` uses local baseline
`f60f0fc0939b3945c4026851e9f4e86e15e2d9be`.

| Qualification | Current evidence |
|---|---|
| Failure-first regression | Final test blobs `565efe921bf081d0189e87d7903f789ffb86ddbd` / `c37bef9044421f46e9e2fca43604065e9d729b7a` were compiled against exact baseline production blobs `461e9e9dd8920e38c0583055ea52ee52894a2ab6` / `6cbde2504622c7676a7f8336844d155ce914e76b`; initialization and cleanup passed and the unchanged implementation failed exactly because its summary still instructed the user to select a mode; target status 1 under `/private/tmp/embed-labs-dc-repository-empty-final.HhJKMu/failure-seal` |
| Real empty ESI context | A valid unique imported ESI Device has its real `<Dc>` element removed before import and reaches the actual Details/DC page through the existing Devices Provider |
| Unsupported empty ESI context | A second real imported zero-mode Device adds an unsupported `<Modules>` structure; warning, selector accessibility, and tooltip all say that the Device cannot be added to an offline Project and direct the user to Device Repository support details |
| Unsupported mode-preview context | A third real imported Device combines unsupported `<Modules>` with two DC modes; valid mode preview shows the support warning, while an invalid second mode retains its configuration error and appends cannot-add/Device Repository recovery |
| Removed-device error state | A random stale Device ID produces an unavailable warning and Device Repository recovery instead of claiming that the missing description is a valid zero-mode Device or can be added to an offline Project |
| Visible empty state | The summary explicitly says no ESI Distributed Clocks operation mode is available and no longer says “Select an operation mode” |
| Validation truthfulness | A supported zero-mode Device uses the existing informational `InfoLabel`; unsupported empty or valid-preview states use a warning; unsupported invalid preview retains the configuration Error/Warning and appends cannot-add/Device Repository recovery; no absent configuration is presented as valid |
| Selector state | Count is zero, current index is `-1`, the combo is disabled, and its line editor remains read-only |
| User and accessibility disclosure | The localized selector description and equal tooltip identify the empty repository state, supported offline Project recovery or unsupported Device Repository recovery, read-only presentation, and no-controller/no-network/no-physical-hardware boundary |
| Mutation boundary | Enable, AssignActivate, SYNC0/SYNC1, and potential-reference-clock controls remain disabled or read-only; no Project exists, `setDcConfiguration()` is unreachable, and no ProjectService mutation or Undo command occurs |
| Context reset | Clearing selection destroys the old page; restoring the same Device recreates the zero-item disabled/read-only state from immutable ESI data |
| Existing non-empty compatibility | The original supported two-mode keyboard preview regression still passes at both scales without persistence or Project mutation |
| Current implementation identity | Production blobs `7038760e04a580e725926ae25a5c0e7ba491cf0c` / `a63c58b599ab0ef4327ca7c5322fc3c1fb54fbd1`; frozen test blobs `565efe921bf081d0189e87d7903f789ffb86ddbd` / `c37bef9044421f46e9e2fca43604065e9d729b7a`; test-plugin SHA-256 `a963dd82d883ffd6d9e43d383aa8f9da6da4b3258088918265497dac6155c939` |
| Focused normal-scale test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-dc-repository-empty-final.HhJKMu/seal-focused-normal` |
| Focused `QT_SCALE_FACTOR=2` test | 3 passed, 0 failed; target status 0 under `/private/tmp/embed-labs-dc-repository-empty-final.HhJKMu/seal-focused-2x` |
| Companion mode-preview tests | Normal and 2x each passed 3 events under the same root's `seal-preview-normal` and `seal-preview-2x` directories |
| Complete EtherCATWorkbench normal scale | 57 passed, 0 failed; target status 0 under the same root's `seal-workbench-normal` directory |
| Complete EtherCATWorkbench 2x scale | 57 passed, 0 failed; target status 0 under the same root's `seal-workbench-2x` directory |
| Six-plugin EtherCAT regression | Core 17, Project 12, Devices 8, Workbench 57, Scan 7, Diagnostics 7; 108 passed, 0 failed in independent LLDB-supervised processes under the same root's `seal-six-suites` directory |
| Existing full-suite diagnostic | Each complete Workbench log retains one pre-existing ProjectExplorer TaskHub soft assertion in `testInvalidProjectPresentationAndLifecycle`; the same diagnostic appears in preceding qualification logs, does not occur in the new DC regression, and does not fail a test or change target status |
| Qualified Qt and test build | Qt 6.11 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed in `qt-creator-build-ethercat-product-qt611`; exactly the 16 allow-listed plugin dylibs are present; Workbench plugin SHA-256 `0d31da4ed84c640897b00380ff1693549a4347d8070cc3f3bff075db5d06b09f` |
| Enabled offscreen startup | PID 45433 remained running for 37 seconds under `/private/tmp/embed-labs-dc-repository-empty-final.HhJKMu/seal-lifecycle-enabled`; intentional SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 45427 remained running for 37 seconds with `-noload EtherCATWorkbench` under `/private/tmp/embed-labs-dc-repository-empty-final.HhJKMu/seal-lifecycle-disabled`; intentional SIGTERM produced target status 15 |
| Process and crash-report cleanup | No residual Embed Labs/LLDB process, new DiagnosticReports file, or matching ReportCrash/CrashReporter/diagnosticd unified-log event after 2026-07-19 17:14:35 +0800 |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, `QT_QPA_PLATFORM=offscreen`, `CRASH_REPORTER_DISABLE=1`, `-no-crashcheck`, and only the process-local Touch Bar LLDB breakpoint; no visible main window or system crash dialog |
| Visual/manual desktop inspection | Not run by design; the issue changes text/state only and all executable qualification was offscreen |
| `WITH_TESTS=ON` all-target build | Not rerun; the known unrelated EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, or hardware access | Not performed or added; this remains local read-only ESI presentation with an offline Project recovery path only for supported Devices |

Qt's empty-combo and contextual-description contracts are documented at
<https://doc.qt.io/qt-6/qcombobox.html> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0's explicit empty-state label is visible in its Type Hierarchy widget:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.
Beckhoff describes operation-mode selection only when the slave offers modes:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>.
The explicit no-mode/offline boundary is Embed Labs Qt-native behavior.

## EtherCATWorkbench repository Device Process Data empty-state qualification

`ISSUE-WB-PROCESS-DATA-REPOSITORY-EMPTY-001` uses local baseline
`8818c6c5c48a0e3c7b1e3a770c758401c926e32c`.

| Qualification | Current evidence |
|---|---|
| Failure-first empty state | Baseline production blobs `21c608c1e549e36d364cf2f7cfe5e9c9ccbb774a` / `95d882ba1d2ff295865f5198916a3a77328bafba` failed the new supported-empty summary assertion with target status 1 under `/private/tmp/embed-labs-process-data-repository-empty.QFzGu0/failure-first` |
| Failure-first invalid state | Independent review added a supported-invalid fixture; the pre-fix summary lacked “validation error” and failed with target status 1 under the same root's `review-failure-first` |
| Real Provider contexts | Six unique ESI files import through the existing Devices Provider: supported/unsupported empty, supported/unsupported populated, and supported/unsupported invalid |
| Empty mapping | All five Process Data models report zero rows; no placeholder SM/PDO is fabricated |
| Supported empty recovery | Information status; one real Add to a valid offline Master creates exactly one undoable Slave with empty Sync Manager/PDO lists |
| Unsupported recovery | Empty and populated unsupported Devices show Warning or preserve Error and are rejected with the Project snapshot unchanged |
| Supported-invalid recovery | Error and first reason remain visible, complete issues remain in the tooltip, Add is rejected, and recovery points to corrected ESI import through Device Repository |
| Removed Device | Unavailable Warning, zero rows, Device Repository recovery, and no Project-add instruction |
| Read-only enforcement | Every cell in all five populated repository tables lacks editable/checkable flags; direct EditRole and CheckStateRole `setData()` attempts return false and preserve values |
| Accessibility and reuse | Contextual accessible descriptions equal tooltips and identify empty/unsupported/invalid/removed/offline boundaries; valid-page reuse removes every stale phrase and restores Ok |
| Existing Project isolation | Repository browsing preserves full snapshot, active Project, and Undo/Redo availability |
| Focused tests | Normal and 2x each 3 passed, 0 failed; target status 0 under `focused-sealed-final-normal` and `focused-sealed-final-2x` |
| Process Data companion | Normal and 2x each 7 passed, 0 failed under `process-data-companion-sealed-normal` and `process-data-companion-sealed-2x` |
| Complete Workbench | Normal and 2x each 58 passed, 0 failed under `workbench-sealed-normal` and `workbench-sealed-2x` |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 58, Scan 7, Diagnostics 7; 109 passed, 0 failed under `six-suites-sealed-sequential` |
| Existing full-suite diagnostic | The known ProjectExplorer TaskHub soft assertion remains confined to the pre-existing invalid-project test and does not fail or affect target status |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; Workbench SHA-256 `42e53a365199b9a734bf94ec10a069552259a3f4d612b3b930186df8f4ff204a` |
| Enabled startup | PID 61420 ran continuously for 37 seconds; intentional passed-through SIGTERM produced target status 15 under `lifecycle-enabled-sealed` |
| Explicitly disabled startup | PID 61431 ran continuously for 37 seconds with `-noload EtherCATWorkbench`; intentional passed-through SIGTERM produced target status 15 under `lifecycle-disabled-sealed` |
| Crash-dialog audit | No residual process, new matching DiagnosticReports file, or matching crash-service unified-log event after 2026-07-19 17:52:11 +0800 |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, offscreen Qt platform, crash reporter disabled, `-no-crashcheck`, and no visible main window or system crash dialog |
| Visual/manual desktop inspection | Not run by design; this issue changes text/state only |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard test include blocker remains outside this issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, or hardware access | Not performed or added; this is an offline imported-ESI presentation and checked offline-Project recovery only |

Beckhoff's Process Data page is the structural comparison for the SM/PDO
tables, not an online-capability claim:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.
Qt's model cardinality, mutation, and localized contextual-description
contracts are documented at
<https://doc.qt.io/qt-6/qabstractitemmodel.html#rowCount>,
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>, and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0's explicit unavailable-state precedent is visible at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.

## EtherCATWorkbench repository Device Startup empty-state qualification

`ISSUE-WB-STARTUP-REPOSITORY-EMPTY-001` uses local baseline
`24576f40eab24825baea29047cda750136a82819`.

| Qualification | Current evidence |
|---|---|
| Failure-first state | With production blobs still exactly `9b3052173109daee22678ae12b98f77725947885` / `f1c06438065bf252d7cc903b6c671798ca768f64`, the new supported-empty assertion failed because the old summary did not contain “No ESI Startup”; initialization and cleanup passed and the target exited with status 1 under `/private/tmp/embed-labs-startup-repository-empty.OSpPLZ/failure-first` |
| Real Provider contexts | Six unique ESI files import through the existing Devices Provider: supported/unsupported empty, supported/unsupported populated-valid, and supported/unsupported populated-invalid |
| Supported empty | Zero real rows and Information; the repository page fabricates no request, while the existing checked Add path can create one undoable offline Slave with empty Startup for later manual editing |
| Unsupported empty | Zero real rows and Warning; the existing topology boundary rejects Add and recovery points to Device Repository support details |
| Supported invalid | Read-only populated preview and Error; the first validation reason stays visible, complete issues stay in the tooltip, Add is rejected, and recovery requires a corrected matching ESI import |
| Unsupported invalid | The validation Error and support restriction are both preserved; Add is rejected as unsupported and the Project remains unchanged |
| Unsupported populated-valid | Read-only populated preview and Warning; unsupported structures prevent Project Add without hiding the imported requests |
| Removed Device | Unavailable Warning, zero rows, Device Repository recovery, and no offline-Project Add instruction |
| Supported populated-valid | Existing read-only catalogue and its three source data-type warnings remain; no Startup request is sent by preview |
| Read-only enforcement | Every populated catalogue cell lacks editable/checkable flags; direct EditRole and CheckStateRole `setData()` attempts return false and preserve all display values |
| Accessibility and page reuse | The contextual table accessible description equals its tooltip and identifies empty/unsupported/invalid/removed/offline/SDO boundaries; reusing one page removes stale context and validation-tooltip text |
| Repository controls | New, Edit, Delete, Move Up/Down, Store/Restore, and enable mutation remain unavailable for every repository Device state |
| Existing Project isolation | Repeated repository browsing preserves the complete Project snapshot, active Project, and Undo/Redo availability |
| Existing checked recovery | Supported-empty Add creates exactly one Slave with zero Startup requests and is undoable to the complete prior snapshot with Redo available; unsupported empty/populated and supported-invalid/unsupported-invalid attempts are rejected with snapshot and Undo/Redo state unchanged |
| Failure-path test cleanup | A scope guard immediately owns the opened Project, clears selection, removes it when still present, and drains posted events even if a later assertion returns early |
| Focused tests | Normal and 2x each passed 3, failed 0, target status 0 under `sealed-focused-normal` and `sealed-focused-2x` |
| Startup companion | Normal and 2x each passed 8, failed 0 under `sealed-startup-companion-normal` and `sealed-startup-companion-2x` |
| Complete Workbench | Normal and 2x each passed 59, failed 0 under `sealed-workbench-normal` and `sealed-workbench-2x` |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 59, Scan 7, Diagnostics 7; 110 passed, 0 failed under `sealed-six-suites` |
| Existing full-suite diagnostic | The known ProjectExplorer TaskHub soft assertion remains confined to the pre-existing invalid-project test and does not occur in the new focused test, fail a test, or alter target status |
| Final source blobs | Production `9a67969dfacba88fe429470b3822330c70dde817` / `2df41415e73ee3506a9cbff1a6b6fa08e59e7746`; tests `5d86453db7cc94fcbf7d8fb2061f772c5e4fedd7` / `e4ff4f2652dee1dd04bb01d8a54325a879f909dd` |
| Test plugin | Workbench SHA-256 `a78e961f1aa3ee029950201ea9205a6e0f80a5171542ea998eb3f3165e78aed0` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; Workbench SHA-256 `59715209a6979a974fe3a96403f035bb8c04e57212a070fdcb7e352451a71552` |
| Enabled startup | PID 69283 remained running for 37 seconds with 37 consecutive samples; intentional passed-through SIGTERM produced target status 15 under `lifecycle-enabled-final` |
| Explicitly disabled startup | PID 70440 remained running for 37 seconds with 37 consecutive samples and `-noload EtherCATWorkbench`; intentional passed-through SIGTERM produced target status 15 under `lifecycle-disabled-final` |
| Crash-dialog audit | No residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching ReportCrash/CrashReporter/diagnosticd event after 2026-07-19 19:31:02 +0800 |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, offscreen Qt platform, crash reporter disabled, `-no-crashcheck`, and only the process-local Touch Bar bypass; no visible main window or system crash dialog |
| Visual/manual desktop inspection | Not run by design; this issue changes text/state only and executable acceptance remained offscreen |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, SDO, controller transport, online state, or hardware access | Not performed or added; this is imported-ESI presentation and checked offline-Project recovery only |

All paths above are relative to
`/private/tmp/embed-labs-startup-repository-empty.OSpPLZ`. Beckhoff's Startup
page supplies only the structural comparison for ordered mailbox download
requests and their fields; Embed Labs does not execute those requests:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.
Qt's real model-cardinality, rejected-mutation, and localized contextual
description contracts are documented at
<https://doc.qt.io/qt-6/qabstractitemmodel.html#rowCount>,
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>, and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0's explicit unavailable-state precedent is visible at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.

## EtherCATWorkbench repository Device EtherCAT empty-state qualification

`ISSUE-WB-ETHERCAT-REPOSITORY-EMPTY-001` uses local baseline
`5c88012561d3317b6d689f31668a0a31c2bd66d1`.

| Qualification | Current evidence |
|---|---|
| Failure-first state | With production blobs still exactly `c637f6fffa956e3e79be20302911b87e52cf41c061294cdce7aee84f78d55f08` / `e414a1557a76ce5150e8a5f1e2b62131787f2548118699c9d45e7d3f2827f011`, initialization and cleanup passed, the old supported-empty summary failed the new “No ESI SyncManager” assertion, and the target exited with status 1 under `failure-first` |
| Real Provider contexts | Four unique ESI files import through the existing Devices Provider: supported/unsupported empty and supported/unsupported populated; a fifth context uses an unresolved/missing Device ID |
| Supported empty | Zero real rows, hidden header-only tree, explicit no-SyncManager summary, no fabricated row, and Device Repository recovery |
| Unsupported empty | Zero rows, hidden tree, unsupported/cannot-add disclosure, and Device Repository support recovery |
| Unsupported populated | Existing two-row read-only preview remains visible while the support restriction prevents Project-add guidance |
| Unresolved/missing Device | Explicit unavailable state, zero rows, hidden tree, and Device Repository recovery |
| Supported populated | Existing seven-column, two-row read-only offline preview remains visible without stale empty, unsupported, or unavailable text |
| Accessibility and page reuse | Summary accessible description and tooltip equal current visible text; tree accessible description and tooltip identify current state; supported empty → unsupported empty → unsupported populated → unresolved → supported populated clears all stale visible state |
| Read-only enforcement | Every restored populated row retains non-editable flags |
| Project isolation | The Project list is empty before and after all repository browsing |
| Focused tests | Normal and 2x each passed 3, failed 0, target status 0 under `review-final2-focused-normal` and `review-final2-focused-2x` |
| Complete Workbench | Normal and 2x each passed 60, failed 0, target status 0 under `review-final2-workbench-normal` and `review-final2-workbench-2x` |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 60, Scan 7, Diagnostics 7; 111 passed, 0 failed under `review-final2-six-suites` |
| Existing full-suite diagnostic | The known ProjectExplorer TaskHub soft assertion remains confined to the pre-existing invalid-project test and does not occur in the focused test, fail a test, or alter target status |
| Final source blobs | Production `0f264b16c975d3ae86fddf5d7b979a0b40ab49039a23fd30ea565992460426b3` / `e414a1557a76ce5150e8a5f1e2b62131787f2548118699c9d45e7d3f2827f011`; tests `944e2ab60c815fd1badf7a453a9acbc10c478ced7ace4c5138e76dda7ba5f5ce` / `1250eacb1b82f94aeb8cef4ddb27e439bd1e689b684c04fb8ca152764b863abe` |
| Test plugin | Workbench SHA-256 `b5f333199be872e658f7592f070bfac50f76722fe0d4b7b743354902b2bb5b5c` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; Workbench SHA-256 `1eee835eec11d74d74b27e0a600306383962040e18391c1cacfbc0adfc7f0835` |
| Enabled startup | PID 50382 remained running for 37 seconds with 37 consecutive samples; intentional passed-through SIGTERM produced target status 15 under `lifecycle-enabled` |
| Explicitly disabled startup | PID 51949 remained running for 37 seconds with 37 consecutive samples and `-noload EtherCATWorkbench`; intentional passed-through SIGTERM produced target status 15 under `lifecycle-disabled` |
| Crash-dialog audit | Completed at 2026-07-19 20:30:44 +0800 under `review-final2-crash-audit`; no residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching ReportCrash/CrashReporter/diagnosticd event after 2026-07-19 20:07:00 +0800 |
| Invisible executable policy | Final success-path tests and lifecycle runs used fresh HOME/settings, inherited DYLD variables cleared, offscreen Qt platform, crash reporter disabled, `-no-crashcheck`, and only the process-local Touch Bar bypass; no visible main window or system crash dialog |
| Visual/manual desktop inspection | Not run by design; this issue changes text/state only and executable acceptance remained offscreen |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, or hardware access | Not performed or added; this is an imported-ESI presentation only |

All paths above are relative to
`/private/tmp/embed-labs-ethercat-repository-empty.mBBHJJ`. Beckhoff's
EtherCAT slave page supplies only the structural Sync Manager comparison:
<https://infosys.beckhoff.com/content/1033/tcsystemmanager/1092536331.html>.
Qt's real tree-row cardinality and localized contextual-description contracts
are documented at
<https://doc.qt.io/qt-6/qtreewidget.html#topLevelItemCount-prop> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Qt Creator
20.0's explicit unavailable-state precedent is visible at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/typehierarchy.cpp#L63-L72>.

## EtherCATWorkbench CoE modal refresh qualification

`ISSUE-WB-COE-MODAL-REFRESH-001` uses local baseline
`7395c4ceefe78896496bf3b83d757f5096550e03`.

| Qualification | Current evidence |
|---|---|
| Failure-first production identity | Production remained exactly SHA-256 `e2d896f08b4872e0385d16f88837da185466e7ebde33b8074fb3e198b73af559` / `885e21c1ebd70e60a4ce5cf8c1f73ec7836ee1bdfbb7c98cb7f5006203c1be78` and git blobs `8f452a525eac3471e61e85628391d1406611e0a8` / `f12f8a4ab4059c99c7c86f3f82d6b106d7ad2b24` |
| Advanced failure first | Initialization and cleanup passed; after a real second same-identity ESI import, accepting the old dialog incorrectly restored Offline state and the target exited status 1 under `failure-first-final/advanced.log` |
| Add failure first | Initialization and cleanup passed; after the same real import path, accepting Yes changed the Project snapshot and the target exited status 1 under `failure-first-final/add.log` |
| Real repository refresh | Each regression imports a unique real ESI twice; the second import reports exactly one updated Device, the exact stable affected Device ID, and exactly one `devicesChanged` signal |
| Advanced stale result | The selected Details context remains stable and refresh restores current Mock/all-object state; accepting pre-refresh source/range/filter controls cannot overwrite it |
| Add stale result | A pre-refresh Yes response cannot add a Startup request; complete structured `ProjectSnapshot` equality is preserved through the refresh and response |
| Page removal | Clearing selection while either dialog is open deletes both the CoE page and its owned dialog safely; Details becomes the None context and Project state remains unchanged |
| Current-context behavior | The existing no-refresh Advanced and Add-to-Startup workflows remain accepted; Add captures value/type/IDs before confirmation and re-queries Project/slave before submission |
| Model and ownership checks | Both new regressions attach `QAbstractItemModelTester`; watchdogs bound each dialog; completion is page-bound and no stale `QModelIndex` crosses confirmation |
| Focused tests | Advanced normal/2x and Add normal/2x each passed 3, failed 0, target status 0 under `post-review-focused` |
| Existing CoE workflow | Normal and 2x each passed 3, failed 0, target status 0 under `post-review-regression/existing-*` after asynchronous-dialog adaptation |
| Complete Workbench | Normal and 2x each passed 62, failed 0, target status 0 under `post-review-regression/workbench-*` |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 62, Scan 7, Diagnostics 7; 113 passed, 0 failed under `post-review-six-suites` |
| Existing full-suite diagnostic | The known ProjectExplorer TaskHub soft assertion remains confined to the pre-existing invalid-project test; it does not occur in either focused test, fail a test, or alter target status |
| Final source SHA-256 | Production `6b8738ff5e7f245ddb2ad746aa2058b8452343471bd86e85cbeee29d66be5478` / `542315c7a5ba4f404ec8d2d2edaee15bc519e9f19a7e64e67cf134d2edf53952`; tests `4e99c8e98709136ccc201bbda4f27259a114ccfe6efbcc156bcd95b8bcc2623e` / `0cce0d1c7555b037c552a34daf9ff7a248fee30b40d6fcd0efd7fb73917019a6` |
| Final git blobs | Production `35ff51e9bcb318a89b1ae31afb238c1bae08fb96` / `e916c21579d01c6558e839632c840c7fc2d2c5af`; tests `fba8651001c7e0a9586042453e3ba75d021fe75e` / `a08d624975edbe81cff4f7f1d11439db2e08a79b` |
| Test plugin | Workbench SHA-256 `fff5d55102c847fe85a2c5b4d86871383d7bc1f1b344bbe57db9f09b64f9defc` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; Workbench SHA-256 `d029befb03a59b8e2c6ef17601b379a988cc87d3607889a31e0d04008545051a` |
| Enabled startup | PID 3445 remained running for 37 seconds; the Workbench plugin was loaded for 34 samples after three startup samples; intentional passed-through SIGTERM produced status 15 under `verified-lifecycle-enabled` |
| Explicitly disabled startup | PID 3439 remained running for 37 seconds with `-noload EtherCATWorkbench`; zero of 37 samples loaded the plugin; intentional passed-through SIGTERM produced status 15 under `verified-lifecycle-disabled` |
| Crash-dialog audit | Completed 2026-07-19 21:53:05 +0800; no residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching crash-service event after 2026-07-19 21:15:56 +0800 |
| Invisible executable policy | Fresh HOME/settings, inherited DYLD variables cleared, offscreen Qt platform, crash reporter disabled, `-no-crashcheck`, and only the process-local Touch Bar bypass; no visible main window under this bounded acceptance policy |
| Crash-audit interpretation | The audit supports the offscreen, crash-reporter-disabled test/lifecycle environment only; it does not claim that every possible visible desktop launch can never produce a system dialog |
| Visual/manual desktop inspection | Not run by design; the executable startup acceptance remained active but offscreen |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, SDO, controller transport, online state, or hardware access | Not performed or added; this is a private UI-lifecycle and checked offline-Project path only |

All paths above are relative to
`/private/tmp/embed-labs-coe-advanced-refresh.20260719`. Qt's asynchronous
dialog guidance is documented at
<https://doc.qt.io/qt-6/qdialog.html#exec>. Qt Creator 20.0's matching
delete-on-close precedent is visible at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L532-L539>.
Beckhoff's CoE Online Advanced Settings supply only the structural source,
range, and filter comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446522251.html>.

## EtherCATWorkbench Startup modal refresh qualification

`ISSUE-WB-STARTUP-MODAL-REFRESH-001` uses local baseline
`065dfbeac038cb4071336492449292879c69b8ee`.

| Qualification | Current evidence |
|---|---|
| Failure-first production identity | Production stayed at SHA-256 `4422c80aac2d1d61f6604bb3635ac18ea3d070bcfcedfdf0a01ad21ddc9d876c` / `a54d0c254dd0460d13e13624cf3e7bc796e17eb09ff3019104081f909279a691` and git blobs `9a67969dfacba88fe429470b3822330c70dde817` / `2df41415e73ee3506a9cbff1a6b6fa08e59e7746` |
| Failure-first result | Initialization and cleanup passed; after a real ProjectService Startup refresh, accepting the old Edit produced `Stale dialog value` instead of `Project refresh wins`; target status 1 under `/private/tmp/embed-labs-startup-failure-first.GAIxiz` |
| Edit stale result | The pre-refresh Edit response cannot overwrite the refreshed request or add another Project command |
| Delete stale result | Removing the original request through ProjectService while confirmation is open cannot make Yes delete the newly selected request |
| Stable merge | Current responses re-query Project/slave state and merge by captured request ID; no model index, row, pointer, or configuration reference crosses the dialog boundary |
| New/Edit/Delete lifecycle | All three paths are page-owned, delete-on-close, and asynchronous; every page context assignment invalidates older results |
| Page removal | Clearing selection while either the Startup parameter dialog or Delete message box is open deletes page and child dialog safely and preserves the Project snapshot |
| ESI defaults | The effective displayed ESI defaults remain the valid no-refresh merge base until the existing Project submission stores them |
| Delete order and selection | Current request order determines successor/predecessor selection; remaining requests are renumbered without changing stored list order |
| Existing workflow | No-refresh New, Edit, Delete, validation, selection, Undo, and Redo remain covered by the complete Workbench run |
| Model and ownership checks | The regression attaches `QAbstractItemModelTester`, uses bounded dialog watchdogs, and verifies both `QPointer` page/dialog pairs become null |
| Focused tests | Normal and 2x each passed 3 events, 0 failed, target status 0 under `final5-focused-*` |
| Complete Workbench | Normal and 2x each passed 63 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 63, Scan 7, Diagnostics 7; 114 passed, 0 failed |
| Existing full-suite diagnostic | The known ProjectExplorer TaskHub soft assertion remains confined to the invalid-project test and does not occur in the focused regression, fail a test, or alter target status |
| Final source SHA-256 | Production `2cf3ebe16293d99758f0501e89cc83def3b46861d15535f11301aa82b2e653d8` / `a743a90d1dc6e917b1fbc61e474b134b8cd9ca04574373e40a61213c3f76825c`; tests `69aaa4d79313a2e9b226a1f86b3752d2d60319d1252a2fadddb4f773a390ac8a` / `669dbf8a72ee85492f4ed543eeab80d4ce65ef34f938258ac1a0936c4731bd10` |
| Final git blobs | Production `b5ad8f7cbd052279e76fa895d621854bb3ff1f86` / `ae44af514be94cf15fd1745231cba12b77527236`; tests `78553aeacb40c91db31fafca0c743b1e613b490a` / `aeaca5a8038871827e01eb69c6ec43ba2402f1ce` |
| Test plugin | Workbench SHA-256 `a645049644badcfe170ed36885cd02c07b1b5994f9b8fe1abf481e755aa74187` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; Workbench SHA-256 `eca1e11f0ca98aa5675b1ee9340f0fdbdaa1d82b204ab0e93cb87751f724cbe1` |
| Enabled startup | PID 28144 remained running for 37 samples and loaded Workbench in all 37; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 30196 remained running for 37 samples with `-noload EtherCATWorkbench`; zero samples loaded the plugin; intentional passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | At 2026-07-19 22:47:52 +0800 there was no residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching crash-service event after 2026-07-19 22:15:00 +0800 |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, and only the process-local Touch Bar bypass; no visible main window or system crash dialog |
| Visual/manual desktop inspection | Not run by design; the issue is lifecycle/data integrity and executable acceptance remained offscreen |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, SDO, or hardware access | Not performed or added; this is a private UI lifecycle and checked offline-Project path only |

All current success-path evidence above is under
`/private/tmp/embed-labs-startup-modal-refresh.UStcax`. Qt's asynchronous
dialog and parent-lifetime contracts are documented at
<https://doc.qt.io/qt-6/qdialog.html#exec> and
<https://doc.qt.io/qt-6/qmessagebox.html#question>. Qt Creator 20.0's matching
delete-on-close precedent is visible at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L532-L539>.
Beckhoff's Startup page supplies only the structural request-list comparison:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.

## EtherCATWorkbench configured-slave tree physical-order qualification

`ISSUE-WB-TREE-PHYSICAL-ORDER-001` is qualified from local baseline
`a9525adcf39b11704d04e2b65da3488897c43f56`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Production remained at SHA-256 `31e486b2b074aa4e6c6fa1c629fc8ea55972f9b29ee82c74175d58711eb7945a` / git blob `3a901d3c1c2a6a6fa005e66d8f592894050fb6f3`; initialization and cleanup passed, but row 0 was `Alpha Physical Second` instead of the position-0 `Zulu Physical First`, and the target exited with status 1 under `/private/tmp/embed-labs-tree-physical-order.tIKzsO/failure-first` |
| Physical source order | A complete configured-Slave sibling group follows `OfflineSlaveConfiguration::position`; renaming a slave does not move it |
| Navigation proxy | The filtered Workbench navigation proxy preserves the source physical order at normal and 2x scale |
| Move commands | Existing real Move Up and Move Down commands update the Project position and the matching visible master child row immediately |
| Reset selection | Stable `NodeId` selection and the current navigation row survive Project-driven model reset and active filtering |
| Duplicate position | Equal positions fall back deterministically to case-insensitive name and stable `NodeId` |
| Missing position | If any sibling Slave lacks an offline configuration, the entire sibling group preserves the original case-insensitive name ordering |
| Model contract | `QAbstractItemModelTester` remains attached throughout the focused source/proxy/reset sequence |
| Focused physical-order tests | Normal and 2x each passed 3 events, 0 failed, target status 0 under `final2-focused-*` |
| Existing topology workflow | Normal and 2x each passed 3 events, 0 failed, target status 0 under `final2-topology-*` |
| Complete Workbench | Normal and 2x each passed 64 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 64, Scan 7, Diagnostics 7; 115 passed, 0 failed |
| Final source SHA-256 | Tree model `3211b55cffb8762f82f22d78e340ce517595b2e39257e038cdd2e9274a0947aa`; test implementation `e86aa10e468855b00ca5952748260d0a1a2614c3fd9729922cf394d0c9a65e36`; test declaration `083670f9dc98e96a2e05f6ededcd842cb7adb1f19f56c4bd3c66783c915e4b15` |
| Final git blobs | Tree model `aa23ee6dfe4e8b4cc6c7b996250592521f3b5c97`; test implementation `5647490e020596fe38cabc413f169143a275c0bb`; test declaration `996d452da5784cab2d282ca49810f32b4936bb72` |
| Test plugin | Workbench SHA-256 `b046bbf6d9e4a649d6ada08afc7a722344582adf52c127f27700b952d9b7b697` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; Workbench SHA-256 `2e182d12b357c467d75b75a406b0cf28344a5eac03e49c514ab5934c272937cd` |
| Enabled startup | PID 75471 remained running for 37 samples and loaded Workbench in all 37; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 78033 remained running for 37 samples; an independent final `vmmap` found no Workbench plugin; intentional passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | At 2026-07-22 08:48:12 +0800 there was no residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching crash-service event after 08:44 +0800 |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, and only the process-local Touch Bar bypass; no visible main window or system crash dialog |
| Visual/manual desktop inspection | Not run by design; product lifecycle acceptance remained offscreen so it could not interrupt desktop use |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard test include blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, Project command, model role, or source-list changes | None |
| Direct upstream Core, ProjectExplorer, or app changes | None; Workbench path count remains 44 and direct upstream Core patch count remains five |
| Network, controller transport, online state, scan, SDO, or hardware access | Not performed or added; this is a private Workbench tree projection over local/offline Mock Project state |

Persisted test, product-build, and lifecycle logs for this issue are under
`/private/tmp/embed-labs-tree-physical-order.tIKzsO`. Beckhoff's physical-ring
and Auto Increment address semantics are documented at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1342524811.html>.
Qt's proxy source-model contract is documented at
<https://doc.qt.io/qt-6/qsortfilterproxymodel.html>. Qt Creator 20.0's Project
tree comparator supplies the host-side semantic-priority-before-name
precedent:
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/projectexplorer/projectmodels.cpp#L88-L103>.

## EtherCATWorkbench topology dialog page-lifecycle qualification

`ISSUE-WB-TOPOLOGY-DIALOG-PAGE-LIFECYCLE-001` is qualified from local
baseline `e814edf65c95ef3c5701f7655c1ff0237cdeac26`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Production remained at `ethercatpage.cpp` SHA-256 `0f264b16c975d3ae86fddf5d7b979a0b40ab49039a23fd30ea565992460426b3` / blob `125159bcec59bb9cc1cf86f880c15a62d405be8e` and header SHA-256 `e414a1557a76ce5150e8a5f1e2b62131787f2548118699c9d45e7d3f2827f011` / blob `23b20b352bdb2945c4b57474f605e2fa2c1d9f63`; a real same-master Project refresh left the stale row visible, so the focused target exited 1 under `failure-first/focused.log` |
| Asynchronous open | Topology uses page-owned heap allocation, `Qt::WA_DeleteOnClose`, and `open()`; the click returns before modal processing |
| Snapshot freshness | Every `EtherCATPage::setContext()` closes the old snapshot, including Project and repository/index refresh; this is conservative invalidation, not live refresh |
| Repeat and reopen | A repeat click raises the current dialog; Close or Escape destroys it, and an immediate same-stack reopen creates a distinct retained dialog |
| Page lifetime | Selection change, project close, and page teardown close and delete the dialog without nested-loop parent deletion |
| Pointer identity | A private `QPointer<QDialog>` is cleared by `finished` only if it still identifies that dialog; an old deferred deletion cannot clear a reopened instance |
| Preserved UI contract | Read-only offline/Mock snapshot, ten columns, configured order, screen bounds, cell accessibility, Close button, and Escape remain qualified |
| Superseded documentation | Prior topology-bounds and cell-accessibility statements about stack or modal lifetime are superseded; their geometry and accessibility evidence remains valid |
| Focused lifecycle tests | Normal and 2x each passed 3 events, 0 failed, target status 0 under `final3-focused-*` |
| Related topology tests | Normal and 2x each passed 11 events, 0 failed, target status 0 under `related-*` |
| Complete Workbench | Normal and 2x each passed 65 events, 0 failed, target status 0 under `final3-workbench-*` |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 65, Scan 7, Diagnostics 7; 116 passed, 0 failed |
| Known soft assertion | The existing invalid-project Workbench path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it is absent from the focused test and does not fail a test or target |
| Final source SHA-256 | Page implementation `2ee8353e49b0f4071d2f43ae3085e5af4b5d78b5035146f41957391e8852cc5d`; page header `8551cd7031c8084c4dcd2de962adfa03827561d9c6c5bd94c0c87514dda8e6b4`; tests `e1fa20d5cc394c7a6999da8d3274a98b6d0633cc67b5c252968e540ce4bdc2cc`; test header `d3771690ff204fc6eafab7a3c7f4231ad461ec6301d288731ae665d86a2670f9` |
| Final git blobs | Page implementation `910465f3a1dde786add222808d502131f5b6bb9e`; page header `dd3751d3b552b6bcb51d8f7aa68e0b9d117e6406`; tests `1a81136d36cfd362562d5b275f21d2ca0a564fd6`; test header `a7c062a6ab301bb0de0470b2c6bdb0ab8a0296da` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench SHA-256 `10ba46cb43718a7c38170d7c0aaae8030f4f8a1fbb5ceb92f0825a5f0a3ee8d8`; test Workbench SHA-256 `5f4fe0b35bb31780000928645fc823af44b7d7993578d88714ef0d07af15e541` |
| Enabled startup | PID 85263 remained running for 37 samples and loaded Workbench in all 37; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 85262 remained running for 37 samples with Workbench absent in all 37; intentional passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | At 2026-07-22 09:27:59 +0800 there was no residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching crash-service event after 09:24 +0800 |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, and only the process-local Touch Bar bypass; no visible main window or system crash dialog |
| Visual/manual desktop inspection | Not run by design; product lifecycle acceptance stayed offscreen so it did not interrupt desktop use |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, source list, dependency, persistence, Provider, ProjectService, Project command, or model-role changes | None |
| Core, ProjectExplorer, app bootstrap, network, controller, online topology, scan, SDO, or hardware changes | None; this remains a private Workbench lifecycle change over local/offline Mock state |

Persisted evidence is under
`/private/tmp/embed-labs-topology-dialog-lifecycle.70Zcr4`. Qt's nested-loop
warning is at <https://doc.qt.io/qt-6/qdialog.html#exec>, and QObject parent
ownership is at <https://doc.qt.io/qt-6/objecttrees.html>. Qt Creator 20.0's
matching asynchronous delete-on-close precedent is at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L527-L540>.
Beckhoff's offline/online Topology semantics are at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1277974411.html>.

## EtherCATWorkbench stale offline-slave removal qualification

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-PROJECT-REFRESH-001` is qualified from local
baseline `312e1a9dbacfd621615c87d644b7a46bb5f5a64e`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Production controller implementation/header stayed at SHA-256 `5fb5b70a377f57c023bc2c9201d715d5cf6ea8e8639c425b231c68a7f01ce552` / `d8212f430dc167713504fb2501d1535200fd85d2ff99d8811b59b483963dc693` and blobs `61c361f0b4c6fd37ca933d47d5852b539118c10a` / `26cb642418687a60608b33183db524d4fc62fa74`; a real same-ID DC refresh followed by stale Yes removed the refreshed Slave, so the focused target exited 1 at the expected assertion in `failure-first/semantic2.log` |
| Private capture | Workbench captures the complete `OfflineSlaveConfiguration` by value: ID, Master, position, identity, serial, Alias, name, device description, Process Data, Startup, DC, and nested fields |
| Revalidation | Yes re-resolves current Project/Master/Slave stable IDs and selection, then exact-compares the target configuration before any mutation |
| Stale response | A changed target returns an explicit error and preserves the refreshed configuration, Slave selection, Undo/Redo availability, and signal count; the user must confirm the current configuration again |
| Unrelated refresh | Another Project or sibling Slave does not invalidate the question while the selected target Slave value remains unchanged |
| Current response | A newly opened current question removes the Slave and remains fully Undo/Redo reversible; the qualified single-Slave case repairs selection to Master, while the existing multi-Slave path selects the adjacent Slave |
| Escape and lifetime | A real Escape is a no-op; all questions use delete-on-close and are observed through guarded pointers until destruction |
| Focused and related | Normal and 2x focused runs each passed 3 events; normal and 2x related removal runs each passed 4 events; 0 failed, status 0 |
| Complete Workbench | Normal and 2x each passed 66 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 66, Scan 7, Diagnostics 7; 117 passed, 0 failed |
| Known soft assertion | The pre-existing ProjectExplorer TaskHub category soft assertion remains confined to the invalid-project path and does not fail a test or target |
| Final source SHA-256 | Controller implementation `0e1a3c43132b186240ced01e69558fd847b97d0b97614b1f805dac2a753e0d32`; controller header `1cd58714e3de3db609886fe0d1dd4b79a66132193c294352c6d9ff69a2e6e9f2`; tests `f6727669cf2d44325693853469fa6b5a9fac67fdd09f55e777faebc1293c7700`; test header `b52558c301ddca51c465809597eaad066f68550e9fa889998c4990d7bc500c4e` |
| Final git blobs | Controller implementation `2d9af3fe0ede52c5146e4faf5335ef1e716eb134`; controller header `9805736ecfa23b4597c749270041e9b3bfd03e90`; tests `1076daadd6da7063394f2dc924544f3f0da8cb86`; test header `ec075cfc2d14e2e7aa0a1d3b054fa29b7ce419d8` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench SHA-256 `d2690915bbe2e089035deb5fa799386f670ec8050505910cdd26a5cf0d1a2b0c`; test Workbench SHA-256 `a0678ed0e865c3e9528756b8b9a9d84e9c0a71d228586d8ecff1fd28c620acf5` |
| Enabled startup | PID 2618 remained running for 37 samples; independent `vmmap` confirmed Workbench loaded; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 7890 remained running for 37 samples with `-noload EtherCATWorkbench`; independent `vmmap` confirmed Workbench absent; intentional passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | At 2026-07-22 10:17:55 +0800 there was no residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching crash-service event after 10:12 +0800 |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, and only the process-local Touch Bar bypass; no visible main window or system crash dialog |
| Visual/manual desktop inspection | Not run by design; product acceptance stayed offscreen so it did not interrupt desktop use |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, scan, online state, SDO, or hardware changes | None; this remains a private Workbench guard over local/offline Mock state |

Evidence is under
`/private/tmp/embed-labs-remove-project-refresh.ErQwPA`. Qt dialog and message
semantics are documented at <https://doc.qt.io/qt-6/qdialog.html> and
<https://doc.qt.io/qt-6/qmessagebox.html>. Qt Creator's official Project
settings source is at
<https://github.com/qt-creator/qt-creator/tree/v20.0.0/src/plugins/projectexplorer>.
Beckhoff's selected-device Remove semantics are at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.

## EtherCATWorkbench asynchronous insertion-dialog qualification

`ISSUE-WB-INSERT-DIALOG-ASYNC-LIFECYCLE-001` is qualified from local baseline
`617505a08cb2055d6042a2580189182ffd39658a`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Production `workbenchmode.cpp` stayed at SHA-256 `24b27a931b9b66a70b0d777f847ce142db0a7d2b2871e4e1d75d13b6b77210db` and blob `ef98dbeebe74ae26bb8dfca23725cac8e81e3df9`; a real global Add New Item action processed dialog events before returning, so the focused target exited 1 at the intended assertion in `failure-first-semantic/test.log` |
| Asynchronous ownership | The mode owns one heap dialog through `QPointer`, sets `Qt::WA_DeleteOnClose`, and calls `open()`; no stack dialog or nested `exec()` remains |
| Repeat action | A second Add New Item action raises the retained selector and leaves exactly one visible selector |
| Target invalidation | Active-Project switch, target close, Project invalidation, and Master removal reject the selector without mutating either Project |
| Accepted path | The selected ESI ID enters the existing guarded `addDeviceToMaster()` and `ProjectService` command path; a destroyed controller makes late completion a no-op |
| Parent teardown | The selector is a mode child and is safely destroyed with that parent; rejection destroys it through delete-on-close |
| Preserved user contract | Device/revision list, filters, Extended Information, supported-device guard, Add/Cancel behavior, Project selection, and Undo/Redo are unchanged |
| Focused and related | Normal and 2x focused runs each passed 3 events; normal and 2x related runs each passed 8 events; 0 failed, status 0 |
| Complete Workbench | Normal and 2x each passed 67 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 67, Scan 7, Diagnostics 7; 118 passed, 0 failed |
| Known soft assertion | The pre-existing ProjectExplorer TaskHub category soft assertion remains confined to the invalid-project path and does not fail a test or target |
| Final source SHA-256 | Mode implementation `f7c7dec383d780c6ff4808effaac7965769b12887b431b876bec16bcfcf59b82`; tests `fa8b490eef12c856ef2a28227edbbded7a2b3116a5ff118292fd706dbcb28fba`; test header `d1b1edff100e48f0a2954e16371aa5b493edbfdfc690a59cb4824297e0a89b56` |
| Final git blobs | Mode implementation `43bfa50de23da414ca2f5a64490157c21fc855a7`; tests `95a868337be859661733bf296f151d1c915ae8c8`; test header `da16b5b302abee755cbaf232dcbdca39c00d74ff` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench SHA-256 `f07f0992512796d39f9326afe973d1098698a8be05cb0b376724765ed24a8742`; test Workbench SHA-256 `90f12f71951615032519130801d54d37c04390ec104b7efa7e0cc39b30001b21` |
| Enabled startup | PID 94229 remained running for 37 samples; independent `vmmap` confirmed Workbench loaded; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 98352 remained running for 37 samples with `-noload EtherCATWorkbench`; independent `vmmap` confirmed Workbench absent; intentional passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | At 2026-07-22 10:58:33 +0800 there was no residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching crash-service event after 10:52:30 +0800 |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, and only the process-local Touch Bar bypass; no visible main window or system crash dialog |
| Visual/manual desktop inspection | Not run by design; product acceptance stayed offscreen so it did not interrupt desktop use |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, scan, online state, SDO, or hardware changes | None; this remains a private Workbench lifecycle change over local/offline Mock state |

Evidence is under
`/private/tmp/embed-labs-insert-dialog-async.aVAVS1`. Qt's asynchronous dialog
and nested-loop guidance is at <https://doc.qt.io/qt-6/qdialog.html>. Qt
Creator's matching ownership precedent is at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/plugins/texteditor/fontsettingspage.cpp#L527-L540>.
Beckhoff's Add New Item and revision-selection workflow is at
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2477595531.html>.

## EtherCATWorkbench General non-conflicting refresh qualification

`ISSUE-WB-GENERAL-NONCONFLICTING-REFRESH-DRAFT-001` is qualified from local
baseline `2fe30622dd971c9c3afd91e78e5addb2fa988340`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Production General implementation/header remained at SHA-256 `a8bee2a329e954482ede4130e3005e70e46888251617acc9d9b23a9378f034fc` / `c7c5b44b0bc7e9c3bbf9acf22b9a102e32e38e4d67bd50954804ded43a330f92` and blobs `b07a339f5512acac70a2c80357183041e250e3c2` / `f72ee71744e19d99f4a950bb514987282260e0d0`; a real ESI rebuild replaced the focused Unicode `%1`, `%2`, `%%` Project draft with the persisted name, so the intended assertion failed and the target exited 1 |
| Stable identity gate | Draft preservation requires equal `projectId`, `nodeId`, node kind, and matching page-owned authoritative baseline |
| Local-edit gate | Only a writable editor that is modified or focused is eligible; focus conservatively avoids disturbing a possible input-method preedit before `modified` changes, while read-only, invalid, unavailable, switched, or closed context reloads authority |
| Non-conflicting refresh | Project, Target, Master, and configured Slave drafts retain exact text, modified state, focus, cursor, selection, and executable local Undo/Redo while their persisted name is unchanged |
| Other data freshness | Repository signals remain connected; configured Slave proves a same-identity ESI update refreshes Type, and Project proves its Target summary refreshes, while the Name editor stays intact |
| Authoritative conflict | Real Project/structural/Slave rename, Undo, and Redo replace the draft and clear `modified` |
| Commit rejection/no-op | Commit forces authoritative reload through synchronous command signals and clears local modified state; an empty rejected name and trimmed no-op cannot be preserved by focus |
| Editor-state safety | The preserved editor is not hidden, cleared, or passed to `setText()`; other General controls continue their normal reset/populate path |
| Refined prior policy | The earlier project-scoped draft issue still filters unrelated Project signals; its statement that a current-Project refresh may replace a draft is narrowed here to a real same-name-field authority change |
| Deliberate exclusions | No autosave, cross-node draft cache, generic dirty-form framework, merge/conflict UI, revision, CAS, Details signal suppression, or repository API change |
| Focused normal and 2x | Each passed 3 events, 0 failed, target status 0 under `focused/normal.log` and `focused/2x.log` |
| Related normal and 2x | Each passed 11 events, 0 failed, target status 0 under `related/normal.log` and `related/2x.log` |
| Complete Workbench | Normal and 2x each passed 68 events, 0 failed, target status 0 under `workbench/normal.log` and `workbench/2x.log` |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 68, Scan 7, Diagnostics 7; 119 passed, 0 failed in isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it is absent from the focused test and does not fail a test or target |
| Final source SHA-256 | General implementation `51195c26f5db3936f2e0ba0935d2564d3b8a0bf2ca95c86328de7d39e6ee16e6`; header `93760569091e384acb8a68451b8c3ae8c2f6ce2f744bbcab700780bc1423aaca`; tests `b37584e021ef2c81cd2500d46a14f2f2f8f7d77e69908f5d613d1311f44e8123`; test header `daa78bddcef5def77301b8c610266f1d9ac78bdea7c04e1a20e0ea1e7b8e8ace` |
| Final git blobs | General implementation `e2cce8dd0a1fe6f5837de15492ead898246bffb8`; header `051abe8c5e068d59351cdd8ea7ceba13607f2558`; tests `4f4d9fd4d17300be8761cbebc696cf3237f6f8ff`; test header `c35e54d7c2e36b24c13a7d4790458d12957c6e7f` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench SHA-256 `03859197305b62f9b06e42e59c9253eefa34cbd112644645d9b0ae413de7fa76`; test Workbench SHA-256 `3ff8a25cd361749e91deff31bf5e41331935b171a612f48cbd7bb61470944d12` |
| Enabled startup | PID 62199 remained running for 37 post-ready samples with Workbench loaded in all 37; every timestamped sample is retained in `lifecycle/enabled-samples.log`; passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 63311 remained running for 37 post-ready samples with Workbench absent in all 37; every timestamped sample is retained in `lifecycle/disabled-samples.log`; passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | At 2026-07-22 12:14:15 +0800 there was no residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching crash-service event |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; no visible main window or system crash dialog |
| IME test boundary | The clean-focused regression verifies the conservative focus gate; it does not synthesize or claim a platform input-method composition session |
| Visual/manual desktop inspection | Not run by design; product acceptance stayed offscreen so it did not interrupt desktop use |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, scan, online state, SDO, or hardware changes | None; this remains a private Workbench form-state change over local/offline Mock data |

Evidence is under
`/private/tmp/embed-labs-general-draft-final.bvD4CU`. Qt's QLineEdit state
contract is at <https://doc.qt.io/qt-6/qlineedit.html#modified-prop>. Qt
Creator 20.0's persisted/volatile aspect precedent is at
<https://github.com/qt-creator/qt-creator/blob/v20.0.0/src/libs/utils/aspects.h#L328-L370>.
Beckhoff's General-tab field model is at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1341899531.html>.

## EtherCATWorkbench ESI revision-toggle selection qualification

`ISSUE-WB-ESI-REVISION-TOGGLE-SELECTION-001` is qualified from local baseline
`42110b4e990baae92fbed37f38ab6aa274b34657`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Production dialog implementation/header remained at SHA-256 `8645fe5679f3155d09a425fc80eabb0cf185f186aaec6706d3f387e1d4291816` / `dfa2a65a2702b38c15da210d99ae0adbe516a639d72238e0061d8c514f7b7b3a` and blobs `d603080f1fd9b9a6c34d8f2ced8bf0cf3f26e36d` / `03d7ea0b6a7acbf8582e7e8e43544e90c4499fe2`; revealing the legacy Zulu revision silently changed the selected stable ID to sorting-first Alpha, so the intended assertion failed and the target exited 1 |
| Visible-row continuity | A selected current revision remains current when previous revisions are revealed or hidden and that row stays visible |
| Hidden-row fallback | If no preferred visible ID exists, the existing first-supported then first-visible fallback remains in force |
| Accepted identity | The Add button stays enabled for the retained supported device and accepting the dialog returns that same stable device ID |
| Preserved behavior | Device/revision contents, latest-revision default, sorting, text filter, Extended Information, status text, limited-device guard, Add/Cancel, controller mutation, and Undo/Redo are unchanged |
| Deliberate exclusions | No cross-dialog selection cache, persistent index contract, repository/model reset policy, revision compatibility change, online lookup, or scan behavior |
| Focused normal and 2x | Each passed 3 events, 0 failed, target status 0 |
| Related normal and 2x | Each passed 10 events, 0 failed, target status 0 |
| Complete Workbench | Normal and 2x each passed 69 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 69, Scan 7, Diagnostics 7; 120 passed, 0 failed |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it does not fail a test or target |
| Final source SHA-256 | Dialog implementation `fd52d33b1bdc7d8b9aa5ce88ddcfe863c16b20054e8d85518ef693a5196148f5`; header `472a6169ba2dfacea350a41b39ce4859b7cb8370244fd60a8c759cbf6c1181fc`; tests `3259aa89d7d388ba1a212e7fef9a24f0c3004a29e00e10259f0196c1e8d4fa53`; test header `8fbe321e0d55aaddb76d51e44c70a7ee34282462753fc95805a46245700b2ccb` |
| Final git blobs | Dialog implementation `5adb883e4c6bb09ebcd9772ab762261523010069`; header `23eac3adaf0335b3590a28a9da7dbc81378e0968`; tests `da747581823bb9366ecbe12140932b872b55e0f0`; test header `272cf0b738477b17f62c7e8da409a1e726a4363a` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `759d4904b9a5dae8734e3aaf2fb3eefa10f47e60facdf9003610655b92a89838`; test Workbench `5514510a42c91149247a90edd4ea7d34bd7db846745e4bb3cecf08bf890edaa8` |
| Enabled startup | PID 40382 remained running for 37 consecutive samples with Workbench loaded in all 37; passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 42145 remained running for 37 consecutive samples with Workbench absent in all 37; passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | During the 2026-07-22 12:39:57 to 12:47:33 +0800 product-lifecycle audit there was no residual product/LLDB process, new matching DiagnosticReports file, or matching crash-service event |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; no visible main window or system crash dialog |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, scan, online state, SDO, or hardware changes | None; this remains a private Workbench selection-continuity change over local/offline Mock data |

Evidence is under
`/private/tmp/embed-labs-esi-revision-selection.pcE5YX`. The Qt current-item
contract is at <https://doc.qt.io/qt-6/qitemselectionmodel.html>. Beckhoff's
latest/older-revision insertion workflow is at
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2477595531.html>.

## EtherCATWorkbench removal-confirmation invalidation qualification

`ISSUE-WB-OFFLINE-SLAVE-REMOVE-CONFIRM-INVALIDATION-001` is qualified from
local baseline `532517066a612c4f5220484cadb830d0c07256c0`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the Workbench test changed; production stayed at SHA-256 `03adb384bfd2e0e31606d9dd8294629718b1061c192764584db63198b25480b4`. After a same-slave DC refresh the old question remained alive, the required null-guard assertion failed, and the target exited with status 1 |
| Selection invalidation | Changing the stable Selection while the question is visible rejects and deletes it without Project mutation |
| Project lifecycle invalidation | Closing the captured Project rejects and deletes the question before stale input can be accepted |
| Complete target invalidation | A change to any field of the captured `OfflineSlaveConfiguration`, or removal of that slave, rejects the question |
| Non-conflicting refresh | A sibling-only DC change and restoration that leave the captured candidate unchanged, plus an unrelated-Project change, all leave the exact current candidate's question visible |
| Current responses | Valid `Yes` removes only the captured slave; `No` and Escape remain non-mutating; complete Project-owned Undo/Redo restoration still passes |
| Defense in depth | The controller still rereads and compares Selection, Project, Master, slave, and the complete configuration after `Yes` before submitting the existing replacement command |
| Focused normal and 2x | Each passed 3 events, 0 failed, target status 0 |
| Related normal and 2x | Each passed 4 events, 0 failed, target status 0 |
| Complete Workbench | Normal and 2x each passed 69 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 69, Scan 7, Diagnostics 7; 120 passed, 0 failed in six isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it is absent from the focused test and does not fail a test or target |
| Final source SHA-256 | Plugin implementation `fcbc7b8af6ad31de240bf8a22d4d5d1ae5e55b0a3a984b971ab0f8c18d2b91c6`; tests `0771a822a557c87d2dd08452dace36d7a81ab53c3c5f1f847407869f95037e43`; test header `9eca5d0743f50a179e02ff9acb11aa42267d87f3c6e67918c8b83f435318f20a` |
| Final git blobs | Plugin implementation `666a446400f704fb74d71db22e035adec318cb27`; tests `ea2465c903d3755a0393bdeac73c9f6b19357e90`; test header `25cbc12231cded1ee57165f66973b025afe7befb` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `3a0d246f1fb331d7691311dd940c3046e87a151a2901b3b876285c9b2a77b221`; test Workbench `9c95c1793d96fbc02aecae643a074d8870b5b05bb8e7c1e7ec36931d365b423f` |
| Enabled startup | PID 18133 remained alive for 37 samples with Workbench mapped in all 37; passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 21782 remained alive for 37 samples with Workbench absent in all 37; passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | From 2026-07-22 13:25:01 to 13:42:51 +0800 there was no residual Embed Labs/LLDB process, new matching DiagnosticReports file, or matching crash-service event |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; the exact launcher/target arguments are retained in `product/lifecycle-command-manifest.txt`; no visible main window or system crash dialog |
| Deliberate exclusions | No single-instance confirmation manager, generic dialog framework, active-Project-switch policy, online/controller action, network, scan, SDO, or hardware behavior |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, application bootstrap, network, scan, online state, SDO, or hardware changes | None; this remains a private Workbench modal-lifecycle correction over local/offline Mock data |

Evidence is under
`/private/tmp/embed-labs-wb-remove-confirm-invalidation-001`; `final2/` is the
authoritative passing test set and earlier iteration logs remain only as audit
history. Qt's dialog and connection contracts are at
<https://doc.qt.io/qt-6/qdialog.html>,
<https://doc.qt.io/qt-6/qmessagebox.html>, and
<https://doc.qt.io/qt-6/qobject.html#connect>. Beckhoff's selected-device
Remove semantics are at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1103121931.html>.

## EtherCATWorkbench Process Data refresh-selection qualification

`ISSUE-WB-PROCESS-DATA-SAME-CONTEXT-SELECTION-001` is qualified from local
baseline `f4da0b18cc48077374ba2528091cdb3aeaad99f5`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the Workbench test changed; production stayed at SHA-256 `1eb40802141f55cfc4125f160e410300cb174e0d5d098a307d3ad863904cf80c` and blob `94832604f9589ee2acdf8b5133ce9c6c91bf328a`; a real repository rebuild changed the selected second Sync Manager to the first, failed the stable-ID assertion, and exited 1 |
| Same-context continuity | Repository rebuild, same-Project rename, and Undo preserve the selected Sync Manager/PDO stable IDs and Statusword content when Project ID, node ID, and node kind are unchanged; a retained PDO follows its fresh owning Sync Manager if reassigned; a manual Status selection inside the same derived RxPDO context also survives a real repository rebuild |
| Missing-ID fallback | Removing the selected mapping falls back to the first available Sync Manager/PDO; Undo retains that still-valid current row |
| Context boundary | Switching from a selected TxPDO context to the derived RxPDO node clears the unrelated IDs and focuses Command; derived-node focusing runs only for the genuine context change and does not override a later manual choice during same-context refresh |
| Project authority | Refresh itself does not change the Project; Process Data mutation, validation, modified state, persistence, Undo, and Redo remain owned by ProjectService/EtherCATProject |
| Deliberate exclusions | No cross-node cache, persistent-index contract, repository signal suppression, refresh-reason API, generic selection service, Project revision, merge, conflict UI, online lookup, or hardware behavior |
| Focused normal and 2x | Each passed 3 events, 0 failed, target status 0 |
| Related normal and 2x | Each passed 8 events, 0 failed, target status 0 |
| Complete Workbench | Normal and 2x each passed 69 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 69, Scan 7, Diagnostics 7; 120 passed, 0 failed in isolated LLDB-supervised processes; the isolated Diagnostics rerun is authoritative and supersedes an earlier parallel attempt that logged seven pass events but did not terminate |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it does not fail a test or target |
| Final source SHA-256 | Page implementation `fdefc48d56337bb156feac5152cea699d34872337d8b831aaf3739700f409d0e`; tests `4460ba1cd1f38bb5c351f683790e5a09d60d6d84442a27f6b07c9c460cd955d3`; test header `9eca5d0743f50a179e02ff9acb11aa42267d87f3c6e67918c8b83f435318f20a` |
| Final git blobs | Page implementation `4812288c373b047ff36174863b30daa986471c41`; tests `04461d6d46f43b3a412466a02a433e2eca9779ec`; test header `25cbc12231cded1ee57165f66973b025afe7befb` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 allow-listed plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `0a711961fc9b841e142dbea24bcf62f8320a027956aa5797be03a55c234411e1`; test Workbench `bfcc8b3e460f6578a5e3f58d56a57588b7c2fda827f5b85f1708a87b75f644c8` |
| Enabled startup | PID 71981 remained alive for 37 samples with Workbench mapped in all 37; passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 74910 remained alive for 37 samples with Workbench absent in all 37; passed-through SIGTERM produced target status 15 |
| Known non-fatal launch message | The disabled run emitted the existing shared-memory initialization message, then remained alive for all 37 samples and produced no crash artifact or service event |
| Crash-dialog audit | From 2026-07-22 14:35:44 to 14:42:08 +0800 there was no residual product/debugger process, new matching DiagnosticReports file, or matching crash-service event |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; no visible main window or system crash dialog |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, scan, online state, SDO, or hardware changes | None; this remains a private Workbench selection-continuity change over local/offline Mock data |

Authoritative final evidence is under
`/private/tmp/embed-labs-wb-process-data-selection-refresh-001.jPkuu0/final2`;
the sibling `failure-first/` and `derived-failure/` directories contain the two
red proofs. Sibling `final/`, `green/`, `product/`, root-level product/hash
files, and the non-isolated `final2/six-ethercatdiagnostics/` attempt are
superseded intermediate audit history. The Qt current-item contract is at
<https://doc.qt.io/qt-6/qitemselectionmodel.html>. Beckhoff's selected Sync
Manager/PDO Assignment relationship is at
<https://infosys.beckhoff.com/content/1033/ps2001-2410-1001/10834607243.html>.

## EtherCATWorkbench Alias non-conflicting refresh qualification

`ISSUE-WB-ETHERCAT-ALIAS-NONCONFLICTING-REFRESH-DRAFT-001` is qualified from
local baseline `8c374cb68b2ea5844c6ca5a51cf31ed8b2aa14a1`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the Workbench test changed; production `ethercatpage.cpp/.h` stayed at SHA-256 `2ee8353e49b0f4071d2f43ae3085e5af4b5d78b5035146f41957391e8852cc5d` / `8551cd7031c8084c4dcd2de962adfa03827561d9c6c5bd94c0c87514dda8e6b4` and blobs `910465f3a1dde786add222808d502131f5b6bb9e` / `dd3751d3b552b6bcb51d8f7aa68e0b9d117e6406`; a real Repository update replaced draft `321` with persisted `3`, failed the assertion, and exited 1 |
| Stable identity and authoritative baseline | Preservation requires the same Project ID, node ID, node kind, configured-slave context, and a fresh Alias equal to the page's last authoritative Alias |
| Local edit gate | Only an enabled Alias whose internal editor is modified or whose spin box/editor owns focus can be preserved |
| Same-context Repository refresh | A real same-identity ESI update preserves the draft, modified state, focus, selection, cursor, and local editor Undo/Redo |
| Other EtherCAT data freshness | The same update refreshes the Type and first SyncManager name while Alias remains a local draft |
| Explicit commit authority | Return commits `321` through the existing controller path; scoped force-authority refresh clears the editor's modified state and normalizes success, rejection, and no-op paths |
| External Alias and Undo authority | An external command to `654` replaces draft `456`; Project Undo restores authoritative `321` instead of reviving the draft |
| Node and Project lifecycle | A draft is not cached across node switch and is destroyed on Project close |
| Deliberate exclusions | No duplicate nonzero Alias conflict rule, autosave, cross-node draft cache, merge/conflict UI, revision/CAS protocol, Repository signal suppression, generic form service, online lookup, or physical-hardware behavior |
| Focused normal and 2x | Each passed 3 events, 0 failed, target status 0 |
| Related normal and 2x | Each passed 10 events, 0 failed, target status 0 |
| Complete Workbench | Normal and 2x each passed 70 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 70, Scan 7, Diagnostics 7; 121 passed, 0 failed in isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it does not fail a test or target |
| Final source SHA-256 | Page implementation `a6ff4c3a5da7a96061ccf7fd42f7426533a46a3628f43047d5fc93e27ab1fba2`; page header `e0517e191523303a45dc7574dbe95f701ddf40c27c18fff27b4e87d11d13d18a`; tests `173639291cee4a476f9878b49aff177943a14f8536b68e05ff26a408de3663e0`; test header `0fd6cf8f6dd36e4c5a0fbd713726e4e2075cd2f5231654620dee18e3c683104c` |
| Final git blobs | Page implementation `cfe4498bd3368d61e906149c9019e96a0e4a05ef`; page header `9f9e1bfd2957b5667e7d0eb64dc762684c5c5903`; tests `204287c739f8c5fca0de6ea1dd4d299006b9eb66`; test header `a5d954d40310141922a3513709700749ce3e2f8d` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `09ce71bb36af84f6a9b5f18681257c06b5e8bfcc88ec08d60d3fa5af61d64cbb`; test Workbench `0ea23b46bf78722d6147c0bfc9753313d7412152df814a386e9fc115df21dac3` |
| Enabled startup | PID 37850 remained alive for 37 samples with Workbench mapped in all 37; passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 41198 remained alive for 37 samples with `-noload EtherCATWorkbench`; Workbench was absent in all 37; passed-through SIGTERM produced target status 15 |
| Known non-fatal launch message | The disabled run emitted the existing shared-memory initialization message, remained alive for all 37 samples, and produced no crash artifact or service event |
| Crash-dialog audit | From 2026-07-22 16:34:14 to 16:38:48 +0800 there was no residual product/debugger process, new matching DiagnosticReports file, or matching crash-service event |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; no visible main window or system crash dialog |
| Visual/manual inspection | Not run by design because executable acceptance had to remain invisible and non-interrupting |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, Modules/Channels, network, ADS, scan, online state, fixed address, Identification, ports, SDO, ESC/EEPROM, or hardware changes | None; this remains a private Workbench draft-continuity change over local/offline Mock data |

Authoritative final evidence is under
`/private/tmp/embed-labs-wb-alias-draft-refresh-001.F7RkQj/final`; the sibling
`failure-first/` directory contains the red proof. Earlier `green/` and
`qualification1/` directories are intermediate audit history. The relevant
Qt contracts are
<https://doc.qt.io/qt-6/qabstractspinbox.html#keyboardTracking-prop> and
<https://doc.qt.io/qt-6/qabstractspinbox.html#editingFinished>. Beckhoff's
EtherCAT tab and Alias/addressing semantics are documented at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1342524811.html>,
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358008331.html>,
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1356630411.html>, and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1257993099.html>.
These references do not change the local/offline boundary into an ESC/EEPROM
or hardware-control claim.

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

## EtherCATWorkbench keyboard context-menu qualification

`ISSUE-WB-NAV-KEYBOARD-CONTEXT-TARGET-001` is qualified from local baseline
`38d0d262f972b6bd1f877b6a2306411a1240cbf7`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the Workbench test changed; production navigation stayed at SHA-256 `f6179d068288eec4f88a8cc83f56b58bb2dbd12373540703aa86266048fad761` / `42b40a81380b510430b93bd89822f7f7afc19bbb1b3085b1c007e2ce260489ee` and blobs `ddc7ffe1809768aec322ef9a379dd0495abc8445` / `3e1c35466c7c816c4386e61ea11d70eebd5c764e`; a real keyboard event moved Master selection to Project, failed, and exited 1 |
| Keyboard target | A real Keyboard reason preserves the Master current index and stable Selection while the menu is visible and after it closes; the Master-only Insert Device action is present |
| Visible-row anchor | Keyboard menu uses the visible intersection of the current row; its vertical origin equals the row-center anchor under normal and 2x scale |
| Off-view fallback | After the current Master is scrolled fully outside the viewport, keyboard menu preserves Master and uses the viewport-center vertical anchor |
| Mouse compatibility | A real Mouse reason on Target changes tree and Selection to Target while the menu is visible; the Master-only Insert Device action is absent and the requested row's vertical anchor is retained |
| Duplicate prevention | Tree and viewport filters consume the real event before the custom-menu signal; existing direct custom-signal callers remain supported by the local connection |
| Focused normal and 2x | Each passed 3 events, 0 failed, target status 0 |
| Related normal and 2x | Each passed 12 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Each passed 71 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 71, Scan 7, Diagnostics 7; 122 passed, 0 failed in six isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it did not fail a test or target |
| Final source SHA-256 | Navigation implementation `84a9aa508d876906a88ff349ef0bd0406bf60f4d6ba7090380d1b834d5ce4b4c`; header `e874a45f55f16a8bd5f1db9113eaaadfc96c36c6ea690bf99fc0740219e45ffc`; tests `888473502f6b22880a813847cab934631d73cfabacf1d46aec2907b4344fc9ce`; test header `0f2472d64258d1ff34095274501853c48e66cbdd221193d477e328cb9278fbc7` |
| Final git blobs | Navigation implementation `8a2c6fc675520a9216815c72f0df6f753d5c5a46`; header `f0ee389be08c1023b25e086b761bc8983e879fbd`; tests `2b52792fcd9eea4c64b1838dc9c7f64bf313a93a`; test header `9798981c041d877ce5dc81539df140317ef02a43` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `5d96db48757190e5ba47df76a9ffffcbdaf237701d7adeec5c9531ba8c1ec563`; test Workbench `25595a0f9a338c62de48565a2bd2877fc701ec64b8e2ad415a1c544c758a6ae4` |
| Enabled startup | PID 35107 remained alive for 37 samples with Workbench mapped in all 37; passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 37010 remained alive for 37 samples with `-noload EtherCATWorkbench`; Workbench was absent in all 37; passed-through SIGTERM produced target status 15 |
| Known non-fatal launch message | The disabled run emitted the existing shared-memory initialization message, remained alive for all 37 samples, and produced no crash artifact or service event |
| Crash-dialog audit | From 2026-07-22 17:19:38 to 17:24:22 +0800 there was no residual qualification process, new matching DiagnosticReports file, or matching crash-service event |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; no visible main window or system crash dialog |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, ADS, scan, online state, SDO, or hardware changes | None; this remains a private Workbench interaction correction over local/offline Mock data |

Evidence is under
`/private/tmp/embed-labs-wb-nav-keyboard-context-target-001.Du2VdU`. Qt's
keyboard context-event contract is at
<https://doc.qt.io/qt-6/qcontextmenuevent.html>. Beckhoff's selected-object
keyboard convention is at
<https://infosys.beckhoff.com/content/1033/tcplccontrol/925416331.html>.

## EtherCATWorkbench preferred Diagnostics status qualification

`ISSUE-WB-STATUS-PREFERRED-DIAGNOSTICS-001` is qualified from local baseline
`0524d2dfbb14cacd6f25c584ede4bcce9fe416c4`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the Workbench test changed; all five production SHA-256 values remained identical before and after the red test. The control showed `MOCK Ready` instead of required `MOCK Config / PREOP`, and the target exited with status 1 |
| Preferred Provider modes | The deterministically preferred Provider projects Config/PREOP, FreeRun/SAFEOP, and Run/OP; a non-preferred Provider update cannot replace it |
| Severity merge | Starting/Stopping are Busy, Failed is Fault, active alarm or Master error is Warning, and higher `StateService` Warning/Error remains authoritative; equal-severity non-Mock Providers retain neutral `Diagnostics` wording for Busy, Warning, and Fault |
| Provider boundary | Local snapshots retain `MOCK`; `mock=false` uses neutral `Diagnostics` and explicitly remains Provider-reported rather than inferred controller or hardware state |
| Lifecycle fallback | Availability loss and recovery, removal, and replacement update immediately; removed-Provider signals are ignored; no Provider falls back to Offline or the remaining generic state |
| Compact details and accessibility | Text, tooltip, disabled menu entry, Project/Master IDs, and accessible description all expose the same preferred semantic state and truthful source boundary |
| Focused normal and 2x | Two normal runs and one 2x run each passed 3 events, 0 failed, target status 0 |
| Related normal and 2x | Two normal runs and one 2x run each passed 8 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two normal runs and one 2x run each passed 72 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 72, Scan 7, Diagnostics 7; 123 passed, 0 failed in six isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it did not fail a test or target |
| Final source SHA-256 | Plugin setup `f704eb2324c0a1cd532e1240cf9f0a2a5322a8135cfc4110848b3615bed8893e`; tests `35ef4c3dfa1ffa0be5dd9dabc2c6c0fa4b9003045256a28a00cdb9fedd61ee67` / `3f6c8f0fd810dbc6988687878f05827001e92898629f82bd08408d80f485ef39`; controller `874a901a3fd7884a80ad323e87325cf634b2439b33cc87af9ea40aea929a5c2d` / `d5d5b9f8d95e6eed11448c810d0faef765ab275a0ff4e3e862fd9658dc1bed3c`; status control `722814e9fedd50b4511974c3f7e20d2e896473d913adc03f130237defbf4a960` / `4903613607ee7e779c287b9903963bd0f6e17c8698c94461493a671c2395b358` |
| Final git blobs | Plugin setup `5b6b305ba88faf63559e5578ea10191a168d7b02`; tests `23fbdb1a8623e0e67085f77aa9ce89483bbaea06` / `41c56a396f40a2a67091762b43b6867f90fea5f7`; controller `c89a718570af005ad64d3770cc1a2f5f1848d5b1` / `1502eb7b21f61f1db7c0d8f5b39e8e46bd064f7b`; status control `fe50e55aa93940ddb5177a14368c0ee16dfe731c` / `dafe0fec1be89d1d203a50cc81b673dc6dc86599` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `f99970b4ee69e009236b0156168caa29e5c2944f7ea7de803d357dc55eeacf91`; test Workbench `20857c91a8e92157677c7c527b6df2a9572e4570ceeb4f038d2c62d6ab9e18ff` |
| Enabled startup | PID 5129 remained alive for 37 post-ready samples with Workbench mapped in all 37; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 7036 remained alive for 37 samples with `-noload EtherCATWorkbench`; Workbench was absent in all 37; intentional passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | From 2026-07-22 18:30:36 to 18:33:08 +0800 there was no residual qualification process, new matching DiagnosticReports file, or matching crash-service event |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; no visible main window or system crash dialog |
| Visual/manual inspection | Not run by design because executable acceptance had to remain invisible and non-interrupting |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, ADS, scan, online state, SDO, or hardware changes | None; this is a private Workbench projection over local/offline Mock or explicitly Provider-reported values |

Evidence is under
`/private/tmp/embed-labs-wb-status-preferred-diagnostics-001.urIOBN`. Beckhoff's
Config/FreeRun status-bar and OP descriptions are referenced only for terms
and interaction at
<https://infosys.beckhoff.com/content/1033/el6201/1037001483.html>.

## EtherCATWorkbench DC non-conflicting refresh qualification

`ISSUE-WB-DC-NONCONFLICTING-REFRESH-DRAFT-001` is qualified from local
baseline `304eaf73d4020b59abba15116868bcb8f50cc9f8`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the new Workbench test changed; production `dcpage.cpp/.h` retained SHA-256 `b1b8c7992332e690f51324abfad22a95edac5d7835715ae8120e36057478359d` / `eeb96e55cb546187a277e79f4c213148ad153a09ea844bfd1fcfca6540137376` and blobs `7038760e04a580e725926ae25a5c0e7ba491cf0c` / `a63c58b599ab0ef4327ca7c5322fc3c1fb54fbd1`; old UI showed `125000` instead of draft `130000`, target status 1 |
| Review-derived failure | With intermediate production SHA-256 `c2df72b72419cc6cffaaa61e7343d5d4f9499a6f530e0178f8655af3b312dc7e` / `94ea851d21149b5ce52a07ecfa23ebc7cb0badc145691ae52cf3447bb4002a46` unchanged, the Project stored the second same-name mode but the rebuilt combo reported current index 0 instead of 1; target status 1 |
| Stable identity gate | Draft eligibility requires the same Project ID, node ID, configured-slave kind, valid Project, and editable page |
| Six delayed fields | Operation Mode name, AssignActivate, SYNC0 cycle/shift, and SYNC1 cycle/shift survive together when each authority is unchanged; immediate enable/reference-clock controls are excluded |
| Field-level authority | Each editor compares only its current authority with its last baseline; a conflict in one field does not invalidate eligible sibling drafts |
| Non-conflicting refresh | Project rename, same-identity ESI update, and immediate reference-clock submission refresh authority without discarding eligible drafts |
| Sibling isolation | AssignActivate and Operation Mode commits start from latest authority, retain the latest reference-clock value, and do not submit sibling editor text |
| Commit/no-op/rejection | Explicit field commit, trimmed no-op, parse rejection, validation rejection, and service failure force that field to current canonical authority |
| External conflict and Undo/Redo | Directly covered for SYNC1 cycle while an unchanged SYNC1 shift draft survives; no claim is made that all six fields were separately conflict-injected |
| Duplicate mode names | After deferred ESI refresh, the second `Sync0` row resolves by complete mode value to `0x0700`, SYNC0 `250000/-1000`, and SYNC1 `500000/1000`; current index remains 1 after submission and a later Project refresh |
| Editor state | Operation Mode Unicode and literal `%1` draft, modified state, focus, selection, cursor, and local Undo/Redo survive; native IME composition was not tested |
| Lifecycle invalidation | Node switch replaces the page from current Project authority; Project close destroys the page and draft; there is no cross-node cache |
| Focused normal and 2x | Two normal runs and one 2x run each passed 3 events, 0 failed, target status 0 |
| Related normal and 2x | Two normal runs and one 2x run each passed 9 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two normal runs and one 2x run each passed 73 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 73, Scan 7, Diagnostics 7; 124 passed, 0 failed in six isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it did not fail a test or target |
| Final source SHA-256 | DC implementation `db028f1b2dd3d4b7a294c0ae7746e27b7e5d3668987d153f4181b0d671f3c916`; header `94ea851d21149b5ce52a07ecfa23ebc7cb0badc145691ae52cf3447bb4002a46`; tests `99efc702283e57a74cc351db6620fc52da1e57c600b1e9c2ad0d0c0e99bf8548`; test header `938fb1d8ef7a354ece0f4d617d0550fb65c32a252b2fc0d6909bcd65cf453bb9` |
| Final git blobs | DC implementation `c63bb1a7fba397866adbd2d3646061e116885005`; header `d48787041ad90bb55b08ba517e1650b0e2edc1fc`; tests `6e998f135f0a305f46df0609519fed250c05f606`; test header `cc97dd0ee1abac520a9aaf24dd8e6df909ae4408` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 plugin dylibs; executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `93d405a9393b10057fe992d08c7c883f587bcc68e373513799d00f7636f489c3`; test Workbench `93069046bdf0bbac3c6e67984dcd1a0c85ec61826a15ac527ad7c1198efa9ba4` |
| Enabled startup | PID 79153 remained alive for 37 samples with Workbench mapped in all 37; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 81410 remained alive for 37 samples with `-noload EtherCATWorkbench`; Workbench was absent in all 37; intentional passed-through SIGTERM produced target status 15 |
| Known non-fatal launch message | The disabled run emitted the existing shared-memory initialization message, remained alive for all 37 samples, and produced no crash artifact or service event |
| Final crash-dialog audit | From 2026-07-22 19:39:27 to 19:44:10 +0800 there was no residual qualification process, new matching DiagnosticReports file, matching crash-service event, visible main window, or system crash dialog; this window includes the final post-documentation focused run |
| Intermediate harness incident | An ASCII-only QTest key helper was mistakenly used for Unicode and produced one Qt Test assertion report; it was quarantined in the evidence root, and no later run produced another assertion crash report. Expected failure-first and test-development failures returned ordinary nonzero statuses; all final matrices returned 0 |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM |
| Deliberate boundary | Local ESI/offline Project editing only; no actual DC synchronization, real-time period, controller connection, transport, online state, or physical clock was exercised |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, ADS, scan, online state, SDO, ESC/EEPROM, or hardware changes | None; this remains a private Workbench interaction correction over local ESI/offline Project data |

Evidence is under
`/private/tmp/embed-labs-wb-dc-draft-refresh-001.6ifOHJ`. Qt's editor contracts
are at <https://doc.qt.io/qt-6/qlineedit.html> and
<https://doc.qt.io/qt-6/qcombobox.html>. Beckhoff's Distributed Clocks pages
are referenced only for terminology and interaction at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>,
<https://infosys.beckhoff.com/content/1033/tcsystemmanager/1092594187.html>,
and
<https://infosys.beckhoff.com/content/1033/ethercatsystem/2469120395.html>.

## EtherCATWorkbench CoE same-context view-state qualification

`ISSUE-WB-COE-SAME-CONTEXT-VIEW-STATE-001` is qualified from local baseline
`2e0bc521205d130ea2d56fe6b88901185977d8c2`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the new Workbench test changed; production `coeonlinepage.cpp/.h` retained SHA-256 `6b8738ff5e7f245ddb2ad746aa2058b8452343471bd86e85cbeee29d66be5478` / `542315c7a5ba4f404ec8d2d2edaee15bc519e9f19a7e64e67cf134d2edf53952` and blobs `35ff51e9bcb318a89b1ae31afb238c1bae08fb96` / `e916c21579d01c6558e839632c840c7fc2d2c5af`; after Project rename the old page returned an empty filter instead of `Mode %1 / 模式`, target status 1 |
| Review-derived failure | With intermediate implementation/header SHA-256 `783b87d4e3ee23dd7080c8c3a3777c5b31cfcc1dfc48f5729258481684f0f5af` / `542315c7a5ba4f404ec8d2d2edaee15bc519e9f19a7e64e67cf134d2edf53952` unchanged, clearing the filter after refresh selected fallback `6072:00` instead of the still-existing hidden anchor `6060:00`; target status 1 |
| Stable context gate | Preservation requires non-`None` plus equal Project ID, node ID, and node kind |
| Preserved page-owned state | Text filter, focus, cursor, selection, Undo availability, Advanced range/Hide flags, Mock/offline source, Mock sample generation, and scalar selected address |
| Fresh Project data | Project rename preserved the view state and Mock sample 2 while the current Project name changed |
| Fresh ESI data | Same-identity ESI update preserved filter/source/Advanced state and sample generation while the new `6072:00` ESI comment became visible |
| Hidden selection anchor | An address that remains in the rebuilt source model survives while the proxy hides it; clearing the filter restores `6060:00` rather than adopting visible fallback `6072:00` |
| Removed-address fallback | If the address no longer exists in the fresh source model, the current valid fallback is adopted or the anchor is cleared |
| Modal generation | Every `setContext()` still increments the context generation; stale Advanced and Add-to-Startup responses remain invalid after same-context refresh |
| Genuine context change | Different Project/node/kind resets filter, Advanced state, source, sample generation, and selection on the same page instance; node switch and Project close also destroy the page with no cross-node cache |
| Deliberate exclusions | That issue did not retain manually edited Mock values; their separate compatible-authority boundary is qualified by `ISSUE-WB-COE-NONCONFLICTING-MOCK-VALUE-REFRESH-001` below. No native IME composition, online CoE, SDO, controller, transport, or hardware behavior is claimed |
| Focused normal and 2x | Two normal runs and one 2x run each passed 3 events, 0 failed, target status 0 |
| Related CoE normal and 2x | One normal run and one 2x run each passed 8 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two normal runs and one 2x run each passed 74 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 74, Scan 7, Diagnostics 7; 125 passed, 0 failed in six isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it did not fail a test or target |
| Final source SHA-256 | Implementation `2faebb9f5095f4540a7dd1f70fc9dcbaf06958ed6101b524bc66615750da69a4`; tests `a7929f7af05a4a64326b993a7bd26993711c1836375682bb99682810bac72e08`; test declaration `eae31102217c668bc4e05849e943fedee8f48912629da5103d268bbeebaa60a4` |
| Final git blobs | Implementation `253744dc05c01ff3a9f0b2fa754bc9cf5e6291cc`; tests `bbae1e0c1ffe70b18c7461eb2ea4c4998ecaf998`; test declaration `c10d27e7c7800630308f7f7212d19f5a0f861f22` |
| Product build and inventory | Full `WITH_TESTS=OFF` build passed; exactly 16 plugin dylibs: Core, CppEditor, Debugger, EasyBoard, EtherCATCore, EtherCATDevices, EtherCATDiagnostics, EtherCATProject, EtherCATScan, EtherCATWorkbench, ProjectExplorer, QmakeProjectManager, QtSupport, RemoteLinux, ResourceEditor, and TextEditor |
| Product hashes | Executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `68f547b1b7256e1ff1017b06ef50335d24c4ebd6c3ef610ddf8bbd176df4400e`; test Workbench `3c0878e9e5065cd2b4258f16ebd403700bc02f48c08e022095fa1a092e193068` |
| Enabled startup | PID 65666 remained alive for 37 samples with Workbench mapped in all 37; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 68023 remained alive for 37 samples with `-noload EtherCATWorkbench`; Workbench was absent in all 37; intentional passed-through SIGTERM produced target status 15 |
| Known non-fatal launch message | The disabled run emitted the existing shared-memory initialization message, remained alive for all 37 samples, and produced no crash artifact or service event |
| Final crash-dialog audit | From 2026-07-22 20:21:05 to 20:23:56 +0800 there was no residual qualification process, new matching DiagnosticReports file, matching crash-service event, visible main window, or system crash dialog |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; launch acceptance was not paused |
| Visual/manual inspection | Not run by design because executable acceptance had to remain invisible and non-interrupting |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, ADS, scan, online state, SDO, or hardware changes | None; this is a private Workbench view-state correction over local ESI/offline Project and Mock data |

Evidence is under
`/private/tmp/embed-labs-wb-coe-view-state-001.4vssYQ`. Qt's model/view and
selection contracts are at
<https://doc.qt.io/qt-6/qabstractitemmodel.html>,
<https://doc.qt.io/qt-6/qsortfilterproxymodel.html>, and
<https://doc.qt.io/qt-6/qitemselectionmodel.html>. Beckhoff's CoE pages are
referenced only for terminology and interaction at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html> and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1446522251.html>.

## EtherCATWorkbench Startup non-conflicting refresh qualification

`ISSUE-WB-STARTUP-NONCONFLICTING-REFRESH-DRAFT-001` is qualified from local
baseline `a835d3340e0a3d644928adb445a8c916fda17e23`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | The authoritative red run used old production `startuppage.cpp/.h` SHA-256 `2cf3ebe16293d99758f0501e89cc83def3b46861d15535f11301aa82b2e653d8` / `a743a90d1dc6e917b1fbc61e474b134b8cd9ca04574373e40a61213c3f76825c` and baseline blobs `b5ad8f7cbd052279e76fa895d621854bb3ff1f86` / `ae44af514be94cf15fd1745231cba12b77527236`; a same-slave non-conflicting refresh emitted 2 model resets instead of 0, target status 1 |
| Stable preservation gate | Same Project ID, node ID, configured-slave kind, editable Project, non-fixed stable request ID, unchanged edited-field authority, and compatible stored/ESI-proposal source are all required |
| ESI proposal commit | A direct inline Comment commit stores the three ESI defaults, publishes the committed cell, closes its editor, and remains undoable back to the unstored proposal |
| Editor state | Unicode plus literal `%1`, text, modified state, focus, selection, cursor, and real `QLineEdit` Undo availability survive eligible refreshes; native IME composition was not tested |
| Repository rebuild | A real `rebuildIndex()` completed with two indexing transitions and one devices reset; the draft survived and model reset count remained zero |
| Project and sibling refresh | Project rename and an external sibling Comment update rendered fresh authority without changing the active draft or its editor state |
| Commit isolation | Return submitted the active value once, emitted a `dataChanged` range covering its cell, retained the latest sibling authority, and closed the editor |
| Structural synchronization | Insertion and removal before the active request plus two actual request moves changed fresh row order with zero model resets; the editor remained bound to the same stable ID |
| Conflict authority | An external change to the edited field caused exactly one reset, discarded the draft, and displayed/stored the external authority |
| Alternate delegate | The editable Transition combo preserved `SO` across a sibling refresh; Escape rejected it and retained authoritative `PS` |
| Context and lifecycle | Escape, node switch, re-entry, and Project close establish no cross-node draft. Deleting a secondary Details page with an active editor destroyed page, table, and editor without Project mutation or Undo/Redo change |
| Model contract checker | `QAbstractItemModelTester` remained attached while remove, insert, move, update, reset, commit, and teardown paths were exercised |
| Focused normal and 2x | One final normal and one final 2x run each passed 3 events, 0 failed, target status 0 |
| Startup-related normal and 2x | One final normal and one final 2x run each passed 7 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | One final normal and one final 2x run each passed 75 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 75, Scan 7, Diagnostics 7; 126 passed, 0 failed in six isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path emitted the pre-existing ProjectExplorer TaskHub category soft assertion; it did not fail a test or target |
| Final source SHA-256 | Startup implementation `99902dbe8615dedc25380b8bbb7117ad54da9d092ec3e12bcf97afba57c5235f`; header `01c85b28ca3b487f425c03380e4f1e6662574512a377c05344cd6157093e6eb6`; tests `e24cfe520837600515ec10e5ef60920a05a3b6e110723e5fb61eed5f309ca277`; test header `6375ec50dffbb2f8f396d6192a19b6e4f4d1171d9286f8e64b4fc41e53f99ba8` |
| Final git blobs | Startup implementation `800204196bc22db2bc7c65dcc1fb7355c1fad0d5`; header `a16b70c44354b519bff0ba30b1ca3efbabc8c8a0`; tests `e5412a895aefddd0d9434061a482406c19ec72c6`; test header `e9ad57e869e10193153b4bc0aa79a5e08df989da` |
| Product build and inventory | The `WITH_TESTS=OFF` product Workbench target built successfully; the existing product bundle contains 16 plugin dylibs. Executable SHA-256 is `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product/test Workbench SHA-256 values are `364ff01532e9a89faba87981ce44fc549c93288fccf2e6e08339c0c30f7cf32e` / `0969db5fdfb705ff03d654c3d33219f5f9dda0e40fde368bf600509725eb19c5` |
| Enabled startup | PID 23631 remained alive for 37 samples with Workbench mapped in all 37; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 25473 remained alive for 37 samples with `-noload EtherCATWorkbench`; Workbench was absent in all 37; intentional passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | From 2026-07-22 21:37:00 to 21:39:28 +0800 there was no residual matching process, new Embed Labs DiagnosticReports file, matching ReportCrash/CrashReporter/diagnosticd event, visible main window, or system crash dialog |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; offscreen main-program runtime acceptance proceeded without pause and did not open, pause, or terminate any visible user instance |
| Visual/manual inspection | Not run by design because executable acceptance had to remain invisible and non-interrupting |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Deliberate boundary | No generic table/draft service, cross-node cache, autosave, CAS/merge protocol, or Modules/Channels completion is claimed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, ADS, scan, online state, SDO, or hardware changes | None; qualification used local ESI/offline Project data only |

Evidence is under
`/private/tmp/embed-labs-wb-startup-draft-001.XNDmkO`. The evidence manifest
identifies the authoritative failure-first log and superseded intermediate
runs. Qt's model, delegate, view-reset, and editor contracts are at
<https://doc.qt.io/qt-6/qabstractitemmodel.html>,
<https://doc.qt.io/qt-6/qabstractitemdelegate.html>,
<https://doc.qt.io/qt-6/qabstractitemview.html#reset>, and
<https://doc.qt.io/qt-6/qlineedit.html>. Beckhoff's Startup page is referenced
only for terminology and ordered-request interaction at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.

## EtherCATWorkbench Process Data non-conflicting refresh qualification

`ISSUE-WB-PROCESS-DATA-NONCONFLICTING-REFRESH-DRAFT-001` is qualified from
local baseline `31b4fb48bd564caa3ad5bd2a0947aa5e9800b38d`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the new Workbench test changed; production `processdatapage.cpp/.h` retained SHA-256 `fdefc48d56337bb156feac5152cea699d34872337d8b831aaf3739700f409d0e` / `55c4cf8b5978f3948fdc42eed8658b5791def9fda57a99cfb79902aa15562bcc` and blobs `4812288c373b047ff36174863b30daa986471c41` / `ad62cb47a8d5c7d92b3057a6577c31d5f901a82a`; the real Repository rebuild emitted 3 model resets instead of 0, target status 1 |
| Stable preservation gate | Same Project ID, configured-slave node and kind, editable mapping, selected unique non-null PDO ID, active unique non-null entry ID, unchanged edited-field authority, and compatible stored/ESI-proposal source are required |
| Field authority | Index, Subindex, Bits, requested Bit Offset, Name, and combined Type/raw-Type/bit-length compare their own authoritative values; sibling fields and rows always refresh |
| Editor state | Unicode plus literal `%1`, text, modified state, focus, selection, cursor, and real `QLineEdit` Undo availability survive eligible refreshes; native IME composition was not tested |
| Repository rebuild | A real `rebuildIndex()` completed with two indexing transitions and one devices reset; the same PDO/entry remained selected, the draft survived, and model reset count remained zero |
| Structural synchronization | A fresh sibling insertion before the active Name emitted exactly one row insertion; its persistent index moved from row 0 to row 1 with no reset and retained its stable entry ID. A second prefix insertion changed the active automatic Bit Offset while preserving its stable persistent index. Remove/move paths are implemented but are not a direct test matrix in this issue |
| Commit isolation | Return submitted the active Name once with one `projectChanged` notification on the already-dirty stack, covered its current cell with `dataChanged`, retained fresh sibling authority, and closed the editor |
| ESI proposal commit | Direct inline Name commit stored the full ESI proposal and returned to the unstored proposal in one Undo step. The existing clean-stack transition emitted two identical Project snapshots; the test checks equality and does not treat them as two writes |
| Alternate delegate | The Type combo retained its local choice across Project rename; Escape discarded it and left authoritative Process Data unchanged |
| Deferred automatic offset display | While an automatic Bit Offset editor stayed open, a prefix Entry changed its calculated `Auto (N)` value without notifying the active column. Escape destroyed the editor, then a covering `DisplayRole` notification published the fresh display without a reset |
| Conflict and source authority | An external same-Name change and an unrelated stored-to-ESI source transition closed the editor without committing the local draft and rendered current authority |
| Context and lifecycle | Node switch created a read-only derived page and discarded the draft. Deleting a secondary Details view with an active editor drained deferred deletion without Project mutation or Undo/Redo change |
| Hide behavior | Production `hideEvent()` preserves the editor unless selection changes or the page leaves its Details stack. Ordinary tab/mode/window hiding is implementation-inspected, not directly exercised by the final test; context departure and page destruction are exercised separately |
| Model contract checker | `QAbstractItemModelTester` remained attached to the original PDO Content model through Repository refresh, row insertion, data updates, commit, conflict, and source transition, up to the context switch that destroyed that model. Derived/restored pages and secondary Details teardown were exercised separately, not under that tester |
| Deliberate exclusions | Explicit persistent/multiple editors, a generic table service, cross-node cache, native IME composition, Index/Subindex/Bits editor repetition, full remove/move test matrix, online CoE/SDO, controller, network, PLC, and hardware behavior are not claimed. Direct editor evidence covers Name, Type, and automatic Bit Offset. Same-context fixed/read-only, missing/duplicate-ID, and removed-PDO/entry transitions are implementation gate paths rather than direct cases; direct read-only evidence is the context switch to a derived page |
| Focused normal and 2x | One final normal and one final 2x run each passed 3 events, 0 failed, target status 0 |
| Process Data-related normal and 2x | One final normal and one final 2x run each passed 8 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | One final normal and one final 2x run each passed 76 events, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 76, Scan 7, Diagnostics 7; 127 passed, 0 failed in six isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path emitted the pre-existing ProjectExplorer TaskHub category soft assertion; it did not fail a test or target |
| Review-derived failure | With intermediate implementation/header SHA-256 `02de2a77eddf28340038366d94cda6b50350f1efd05302d522b1bd3ff1f80fc4` / `ab7d21d3820117780abc31c232e99b7541fd38138378d4ab2fce4deffdf786ad` unchanged, automatic Bit Offset changed after a prefix insertion but the post-Escape notification lacked `DisplayRole`; target status 1 |
| Final source SHA-256 | Process Data implementation `229c2660bddc89c6bf7d5cafb481fb6a646ec672ea777cc915421aa9402ad955`; header `ab7d21d3820117780abc31c232e99b7541fd38138378d4ab2fce4deffdf786ad`; tests `56da283f56109fc90aa2ae36ff23f5966ce90f598f3409099a00528e085abf60`; test header `9e3ccd91a9a9411895fc51684dd55b0951937155833a0ed213ddd4e166e582ae` |
| Final git blobs | Process Data implementation `f4e3bf0c571e72607c4a3ecd88fb6541dbb3b275`; header `dc86720292c451a968b14554b8618513b5f1938d`; tests `075546eaaf460b3f4bee31dbb1018eee7bbf7cc3`; test header `92df5cf6855728a4cc20f8c7396853e3f32dbf25` |
| Product build and inventory | The `WITH_TESTS=OFF` product Workbench target built successfully; the existing product bundle contains 16 plugin dylibs |
| Product hashes | Executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `61f62e15fd6718c982dccae52a02f81ddb8186e7be568757dec10dd2f4ec5af0`; test Workbench `add102b915212c371528217f3867a61409a5cdc889e9d452196f9bcb16b9e3d5` |
| Enabled startup | PID 334 remained alive for 37 samples with Workbench mapped in all 37; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 1849 remained alive for 37 samples with `-noload EtherCATWorkbench`; Workbench was absent in all 37; intentional passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | From 2026-07-22 22:51:29 to 22:53:50 +0800 there was no residual matching process, new Embed Labs DiagnosticReports file, or matching ReportCrash/CrashReporter/diagnosticd event |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, crash reporter disabled, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; full main-program runtime acceptance proceeded without pause and did not open, pause, or terminate a visible user instance |
| Visual/manual inspection | Not run by design because executable acceptance had to remain invisible and non-interrupting |
| `WITH_TESTS=ON` all-target build | Not rerun; the unrelated known EasyBoard `extensionmanager_test.h` blocker remains outside this private Workbench issue |
| qbs execution | Not run; no CMake or qbs file changed |
| Public API, dependency, persistence, Provider, ProjectService, Project command, public model role, or source-list changes | None |
| Core, ProjectExplorer, app, network, ADS, scan, online state, CoE/SDO, PLC, controller, or hardware changes | None; qualification used local ESI/offline Project data only |

Evidence is under
`/private/tmp/embed-labs-wb-process-data-draft-001.lx7VcX`. Qt's model-reset,
view-reset, persistent-index, row-move, and line-edit contracts are at
<https://doc.qt.io/qt-6.8/qabstractitemmodel.html#beginResetModel>,
<https://doc.qt.io/qt-6.8/qabstractitemview.html#reset>,
<https://doc.qt.io/qt-6.8/qpersistentmodelindex.html>,
<https://doc.qt.io/qt-6.8/qabstractitemmodel.html#beginMoveRows>, and
<https://doc.qt.io/qt-6.8/qlineedit.html#text-prop>. Beckhoff's Process Data
page is referenced only for hierarchy, fixed-mapping, and interaction
terminology at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.

## EtherCATWorkbench CoE non-conflicting Mock-value refresh qualification

`ISSUE-WB-COE-NONCONFLICTING-MOCK-VALUE-REFRESH-001` is qualified from local
baseline `925b14062f93403cfc9f9552dcaec1e3015ba662`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the new Workbench test changed; production `coeonlinepage.cpp/.h` retained SHA-256 `2faebb9f5095f4540a7dd1f70fc9dcbaf06958ed6101b524bc66615750da69a4` / `542315c7a5ba4f404ec8d2d2edaee15bc519e9f19a7e64e67cf134d2edf53952` and blobs `253744dc05c01ff3a9f0b2fa754bc9cf5e6291cc` / `e916c21579d01c6558e839632c840c7fc2d2c5af`; after a real Project rename the old page returned `08` instead of accepted `5A`, target status 1 |
| Stable context gate | Preservation requires non-`None` plus equal Project ID, node ID, and configured-slave node kind |
| Object authority gate | Scalar address, offline bytes and width, parsed type, raw type, writable flag, and process-data role must match; the current object must remain writable and non-synthetic |
| Accepted values only | Only edits already accepted by model `setData()` qualify for Mock-value replay; the separate CoE inline-draft boundary covers an uncommitted active editor |
| Empty offline baseline | An accepted arbitrary non-empty width is retained only while the baseline stays empty and all other authority matches; explicit Update List restores empty |
| Project rename | A real rename retained `6060:00 = 5A` and the empty-baseline `6061:00 = C0DE` without changing the Project through either edit |
| Same-identity ESI refresh | Both overrides survived while the fresh `6072:00` sibling comment became visible |
| Mock/offline toggle | Offline showed authoritative `08` and empty bytes; returning to Mock restored `5A` and `C0DE` |
| Add to Startup | Confirmed add copied the exact retained `5A`; Undo removed the request while the unrelated retained `C0DE` remained |
| Explicit Update List | Mock Update List cleared both overrides and generated `09`; Offline Update List also cleared an accepted `B6`, so returning to Mock remained `09` |
| Authority conflicts | Changed offline value/width discarded `6B`; a fixed/read-only transition discarded `7C`. Parsed type, raw type, and process-data conflicts share the same conjunctive implementation gate and were not repeated as separate ESI variants |
| Object removal | Removing `6060:00` discarded `3D`; restoring the ESI did not recover it |
| Context and teardown | Switching to the Project destroyed the page and discarded `4E`; re-entry showed `08`. Project close destroyed the replacement page with no cross-node cache |
| Model contract | Source and proxy `QAbstractItemModelTester` instances covered definition resets, Mock refreshes, source toggles, Repository refreshes, object removal, and page teardown |
| Focused normal and 2x | One final run at each scale passed 3 events, 0 failed, target status 0 |
| Related CoE normal and 2x | One run at each scale passed 9 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two runs at each scale passed 77 events per run, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 77, Scan 7, Diagnostics 7; 128 passed, 0 failed in six isolated LLDB-supervised processes |
| Known soft assertion | The existing invalid-project path still emits the pre-existing ProjectExplorer TaskHub category soft assertion; it did not fail a test or target |
| Final source SHA-256 | Implementation `4f6a83237a8be1158799242f5e26f31490ee7559349c1aabc9fddc0d95c5fe0d`; header `ef599680bc35e50556b8f222b06eb7b2672dfac01324ef5993e0a857d5ddb66a`; tests `e996ee623817b12b7f0a062506f797e6e32f4d095bb0fa656126e58a6c906422`; test header `7494c2ea202151a14df773805f503e397a42fd284a5dba1a32ec9bb2b9d53e49` |
| Final git blobs | Implementation `c758168bf1fdc7a02634e22546b14d5c54978ae3`; header `83d07f6d006009bebdf70b2a087d8ce7b2231a63`; tests `c95fbaf75767c46fd8a665188633f99b6c089096`; test header `3441528f22135922229cb267572591db59bb24f0` |
| Product build and inventory | Complete `WITH_TESTS=OFF` build passed; exactly 16 plugin dylibs are present |
| Product hashes | Executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `b68ebb80352e21e6b6fa558b6708fc3859cf785c96d220c12f838e338c383fc0`; test Workbench `e15232b2d8209a1151dd37591a1567f0caa9390ac98ecf0c8ac1241889d9fda9` |
| Enabled startup | PID 96279 remained alive for 37 samples with Workbench mapped in all 37; intentional passed-through SIGTERM produced target status 15 |
| Explicitly disabled startup | PID 97897 remained alive for 37 samples with `-noload EtherCATWorkbench`; Workbench was absent in all 37; intentional passed-through SIGTERM produced target status 15 |
| Crash-dialog audit | From 2026-07-22 23:34:30 to 23:36:52 +0800 there was no residual qualification process, new Embed Labs DiagnosticReports file, or matching ReportCrash/CrashReporter/diagnosticd event |
| Invisible executable policy | Fresh HOME/settings, cleared inherited DYLD variables, offscreen Qt, disabled crash reporting, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; acceptance continued without opening or touching a visible user instance |
| Known non-fatal launch message | The disabled run emitted the existing shared-memory initialization message, remained alive for all 37 samples, and produced no crash artifact or service event |
| Deliberate exclusions | This accepted-value issue adds no active-editor mechanism itself; the separate CoE inline-draft boundary now provides that private behavior. Persistence, cross-node cache, generic merge/CAS service, public API or model role, Provider/ProjectService contract, network, ADS, SDO, controller, and hardware behavior remain excluded |
| qbs execution | Not run; no CMake or qbs description changed |

Evidence is under
`/private/tmp/embed-labs-wb-coe-mock-value-refresh-001.Z9NXQ5`. Qt's model
reset contract is at <https://doc.qt.io/qt-6/qabstractitemmodel.html>.
Beckhoff's CoE page is referenced only for object-value, RW/RO, Offline-value,
and Update List terminology at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.

## EtherCATWorkbench CoE Mock edit-rejection feedback qualification

`ISSUE-WB-COE-MOCK-EDIT-REJECTION-FEEDBACK-001` is qualified from local
baseline `8a0373032093f05ba57af57d70a8cb1bfae404e4`.

| Qualification | Current evidence |
| --- | --- |
| Failure-first | Only the new real-editor test changed. Production `coeonlinepage.cpp` retained SHA-256 `7f0c8d977575d87f7ad08c6a94fcd0708f03c2d59f5e6891aa719828c4688d15` / blob `8351fff476b0edfed51a09f11c9185dfe81f9c7c`, and the header retained SHA-256 `8efa640a8cf8fad46d396dd22269c7c1a5ea68a448179cd67745e6cb1fc433e2` / blob `ee362007d7152e33df3eb8e10f9655a563ab200f`. Invalid Return closed the editor without visible feedback while `6060:00` stayed `08` and Project/Undo/Redo stayed unchanged; 2 passed, 1 failed, target status 1 |
| Rejection categories | Empty input, incomplete final byte, illegal hexadecimal input, and fixed-width mismatch produce complete address-specific messages on the existing CoE operation-feedback surface |
| Invalid submission | Return closes the transient editor, keeps the last accepted bytes, emits no source or proxy `dataChanged`, does not mutate the Project, and creates no Undo/Redo entry |
| Guidance lifetime | Reopening the same object and Escape retain the error. A valid Return or explicit selection, Mock/Offline source, Update List, context, Repository, or teardown boundary clears it |
| Valid normalization | `0x` plus embedded spaces, underscores, and colons is accepted and normalized; the final test commits `0x 5_:A` as `5A` exactly once through both source and proxy models |
| Empty baseline | A writable object with empty baseline bytes accepts the complete non-empty value `C0DE`, preserving the established arbitrary-width rule |
| Read-only isolation | Offline, Repository, read-only, and synthetic cells remain non-editable and do not fabricate edit-rejection feedback. A direct programmatic invalid `setData()` also does not fabricate page feedback |
| Accessibility | Feedback carries a stable accessible name, current description, and matching tooltip. A polite `QAccessibleAnnouncementEvent` is covered only when Qt accessibility support is enabled; no manual VoiceOver or audible-speech result is claimed |
| Model and Project isolation | Invalid cases preserve accepted bytes and produce zero source/proxy changes. The valid case produces one source and one proxy change. Project snapshot and Undo/Redo state remain unchanged throughout Mock editing |
| Context and lifecycle | Selection, source, Update List, same/different context, actual Repository import/provider refresh, and page teardown establish explicit clear boundaries; teardown is safe with no stale editor or feedback owner |
| Focused normal and 2x | One reviewed run at each scale passed 3 events, 0 failed, target status 0 |
| Related CoE normal and 2x | One final run at each scale passed 11 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two final runs at each scale passed 79 events per run, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 79, Scan 7, Diagnostics 7; 130 passed, 0 failed, every target status 0 |
| Known non-fatal diagnostics | Complete Workbench retains the pre-existing ProjectExplorer TaskHub soft assertion. Deliberate attempts to edit non-editable cells emit expected warnings. Neither condition failed a test or target |
| Superseded build artifacts | `failure-first/build.log` used an invalid `InfoLabel` `Q_OBJECT` assumption before `failure-first/build-valid.log`; `green/build-expanded.log` contains a test-only QStringBuilder compile error. The first green build command created no log because its tee directory did not exist, although the target built; `green/build-first-valid.log` is authoritative. No superseded attempt is counted as final qualification |
| Final source SHA-256 | Implementation `2af4e22ce91f194055698817f3c3504caf2ae28696ed3425fd52ed86e6799454`; header `72d57a183c1bb8455b9b823a4ea1c1b838ac037e19cc3bdb9224083c43f38d62`; tests `f0c53efc78c78d66a5e9631fea47fded4c022139de36fe102a5719ec9743d800`; test header `bbb38750f9c380f07ed285633c690a3e917af2afde1dccea91acc254c9db8c24` |
| Final git blobs | Implementation `4cc42ba6a5c491cd29ebe4b6b7154aaba04a0be3`; header `5c677b2f40018f430e8c454fe6ed7b62b77bbc1d`; tests `61c554e8c9f32fcd44054bd7d47e047dc9122345`; test header `ced27e5b83c9c0031ce62b409e710141f56a1a81` |
| Product build and inventory | The `WITH_TESTS=OFF` Workbench target and complete product build both passed; exactly 16 plugin dylibs are present |
| Product hashes | Executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `d6c5becbcdae5850f8238ec8bb873c35e9a5dafe71ed634e2475aa7abd3b551e`; test Workbench `cd18428efb63c8ea668287dee1c1cc045a1a420fea3f7e622ccbf435a45f8abd` |
| Enabled startup | PID 36133 remained alive for 37 of 37 samples with Workbench loaded in all 37; intentional passed-through SIGTERM produced expected target status 15 |
| Explicitly disabled startup | PID 37563 remained alive for 37 of 37 samples with `-noload EtherCATWorkbench`; Workbench was loaded in 0 samples; intentional passed-through SIGTERM produced expected target status 15 |
| Crash-dialog audit | From 2026-07-23 01:23:54 to 01:26:15 +0800 there were zero residual qualification processes, new matching DiagnosticReports files, or matching crash-service events |
| Invisible executable policy | Fresh HOME/settings, offscreen Qt, disabled crash reporting, process-local launch safeguards, and passed-through SIGTERM kept main-program acceptance invisible and non-interrupting; no visible/manual UI inspection was run |
| Deliberate exclusions | No public API/role, Project format or mutation, Modules/Channels completion, Provider/ProjectService contract, network, ADS, scan, online CoE, SDO, controller, PLC, or hardware behavior is claimed |
| CMake/qbs execution | Neither description changed, so qbs was not run |

Evidence is under
`/private/tmp/embed-labs-wb-coe-edit-feedback-001.vl5Suz`. Qt's model and
delegate edit contracts are at
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData> and
<https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData>. The optional
accessibility event is documented at
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. Beckhoff's CoE
page is referenced only for Value, RW/RO, Offline-value, and Update List
terminology at
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345267851.html>.

## EtherCATWorkbench visible-identity filter qualification

`ISSUE-WB-NAV-FILTER-VISIBLE-IDENTITY-001` is qualified from local baseline
`c6f48495263f38a292a780f0955468bf183c692a`.

| Qualification | Current evidence |
| --- | --- |
| User-visible defect | Repository tooltips showed Product `0x0000102a`, but filtering with that exact copied value produced no match because the private search role omitted `0x` |
| Failure-first | Only the new Workbench test changed; production `workbenchtreemodel.cpp` retained SHA-256 `3211b55cffb8762f82f22d78e340ce517595b2e39257e038cdd2e9274a0947aa` and blob `aa23ee6dfe4e8b4cc6c7b996250592521f3b5c97`; initialization and cleanup passed, the exact visible Product filter failed, and the target exited 1 |
| Formatting authority | Vendor, Product, and Revision search tokens now use the same zero-padded `0x........` form as the visible tooltip |
| Compatibility | The complete legacy `Vendor Product Revision group` segment remains unchanged, so both single-field and compound unprefixed searches still match; display-name, status, and provider-detail terms are unchanged |
| Navigation state | Exact visible-identity filtering retains the selected stable device ID and unchanged Project context, shows its Repository ancestor, and hides unrelated devices |
| Focused 1x/2x | Each final run passed 3 events, 0 failed, target status 0 |
| Navigation-related 1x/2x | Each final run passed 6 events, 0 failed, target status 0 |
| Complete Workbench 1x/2x | Two final runs at each scale passed 80 events per run, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 80, Scan 7, Diagnostics 7; 131 passed, 0 failed, every target status 0 |
| Known non-fatal diagnostics | Complete Workbench retains the pre-existing ProjectExplorer TaskHub category soft assertion in the invalid-project path. The explicit-disabled launch emitted `QSharedMemory::handle: doesn't exist`, then remained alive for 37/37 samples and ended with expected status 15. Neither condition failed a test, target, or crash audit |
| Final source SHA-256 | Tree model `96d94e79cb5c938486ac9e81da0d7776614d6e2eb38141ed14a9f3f7fb9c7e71`; tests `47e92628cce83e4ebc0874e4d5882ad112b397b1fa45e6fa0ce0a762fbdc481c`; test header `16070acc603d9394dd1c49fb38c333ce2b6d6e7e15335c4e9bbca08a35ddbf39` |
| Final git blobs | Tree model `042a51ec04dd84371cde46de18c7e6f98e00f348`; tests `0daf1f517d8256e193dd372f6bf82767ab72071b`; test header `8f2940e28febba3cfa5bdafde2d289c6fab24f92` |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | The `WITH_TESTS=OFF` Workbench target and complete product build passed in `qt-creator-build-ethercat-product-qt611`; exactly 16 plugin dylibs are present |
| Product hashes | Executable SHA-256 `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `2d31365c7181be49486199c403117a6e439fcd6f018bf6a68bad600facb6ae45`; test Workbench `74b736da82d453ab68effd574c56b7e4a37cf27fdd53d206b1bbf308b531bff2` |
| Enabled/disabled lifecycle | Both product runs stayed alive for 37/37 samples; Workbench was loaded in 37 enabled samples and 0 explicit-disabled samples; both ended with expected passed-through SIGTERM status 15 |
| Crash-dialog audit | From 2026-07-23 02:04:44 to 02:07:05 +0800 there were zero residual qualification processes, new matching DiagnosticReports files, or matching crash-service events |
| Invisible executable policy | Fresh HOME/settings, offscreen Qt, disabled crash reporting, cleared inherited DYLD variables, `-no-crashcheck`, and the process-local Touch Bar bypass kept acceptance invisible and non-interrupting; no visible/manual UI inspection was run |
| Deliberate exclusions | No public API/role, Project mutation, Repository mutation, Provider/ProjectService contract, Core/ProjectExplorer hook, network, ADS, scan, online state, CoE/SDO, PLC, controller, or hardware behavior is claimed |
| CMake/qbs execution | Neither description changed, so qbs was not run |

Evidence is under
`/private/tmp/embed-labs-wb-nav-visible-identity-001.2G4bre`. Qt's configured
filter-role and recursive-filter behavior is documented at
<https://doc.qt.io/qt-6.11/qsortfilterproxymodel.html>. Beckhoff's identity
field definition is at
<https://infosys.beckhoff.com/content/1033/tcplclib_tc2_ethercat/57119371.html>.

## EtherCATWorkbench blank-area context-menu qualification

`ISSUE-WB-NAV-MOUSE-BLANK-CONTEXT-001` is qualified from local baseline
`142225dc80ec87032f110d96e4408f425477ff65`.

| Qualification | Current evidence |
| --- | --- |
| User-visible defect | A mouse right click on unused tree viewport space reused the prior current index, so a selected Master or slave leaked node-specific commands into a blank-area menu |
| Failure-first | Only the existing Workbench test changed; production navigation stayed at SHA-256 `84a9aa508d876906a88ff349ef0bd0406bf60f4d6ba7090380d1b834d5ce4b4c` and blob `8a2c6fc675520a9216815c72f0df6f753d5c5a46`; a real in-viewport invalid hit exposed a node action and the target exited 1 |
| Empty mouse context | An invalid `indexAt()` result inside the viewport produces an empty menu context without clearing or replacing the current index or stable Selection |
| Generic commands | Expand and Collapse remain present for blank space; global locate behavior retains its existing authority |
| Node command isolation | Insert, Add, Remove, Move Up, Move Down, Set Active Project, and Copy are absent and disabled for the lifetime of a blank-area popup |
| Command restoration | Every node command's enabled state after close equals its state immediately before the blank popup |
| Valid mouse compatibility | A real mouse event on Target still changes both tree and Selection to Target, omits the Master-only Insert action, and retains the valid-row anchor |
| Keyboard compatibility | A real keyboard event still targets the current Master and uses its row, or the viewport center when that row is fully off-view |
| Internal sentinel compatibility | The existing direct `customContextMenuRequested(QPoint(-1, -1))` convention remains current-selection based because the point is outside the viewport |
| Focused normal and 2x | Each authoritative run passed 3 events, 0 failed, target status 0 |
| Related normal and 2x | Each authoritative run passed 12 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two authoritative runs at each scale passed 80 events per run, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 80, Scan 7, Diagnostics 7; 131 passed, 0 failed, every target status 0 |
| Known soft assertion | Complete Workbench retains the pre-existing ProjectExplorer TaskHub category soft assertion in the invalid-project path; it did not fail a test or target |
| Final source SHA-256 | Navigation `be3fb065bc0b9351314eb98ac4e315482159a28e6527d121c1958fc398479da3`; tests `9deeac93fd33988f17be1fde20fe51b8ca794fc3309e113c19cae93fa7f07ad4`; unchanged test header `16070acc603d9394dd1c49fb38c333ce2b6d6e7e15335c4e9bbca08a35ddbf39` |
| Final git blobs | Navigation `cd0ce1ad326832165cf99abf7ed788ee68289f78`; tests `b92893fbd3bd1a890caaaa0b4f57f2c1e51c9dc3`; test header `8f2940e28febba3cfa5bdafde2d289c6fab24f92` |
| Product build and inventory | The `WITH_TESTS=OFF` Workbench target and complete product build passed; exactly 16 plugin dylibs are present |
| Product hashes | Executable `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `b5d6e148b3855adfcea5a835464e90539d848e4469259c91420c29c6cdb55373`; test Workbench `081f8dbef2464f8f5de1dd5f3a53170d658e2281c597a2a01c71b355d87dd003` |
| Enabled/disabled lifecycle | Both product runs stayed alive for 37/37 samples; Workbench was loaded in 37 enabled samples and 0 explicit-disabled samples; both ended with expected passed-through SIGTERM status 15 |
| Crash-dialog audit | From 2026-07-23 02:42:29 to 02:44:52 +0800 there were zero residual processes, new matching DiagnosticReports files, or matching crash-service events |
| Invisible executable policy | Fresh HOME/settings, offscreen Qt, disabled crash reporting, cleared inherited DYLD variables, `-no-crashcheck`, and the process-local Touch Bar bypass kept acceptance invisible and non-interrupting |
| Known non-fatal launch output | Explicit-disabled startup emitted the existing shared-memory initialization message, then remained alive for 37/37 samples and ended with expected status 15 |
| Public/API/system boundary | No public API/role, source list, dependency, Project/Repository mutation, persistence, Provider/ProjectService contract, Project command, Core/ProjectExplorer hook, application bootstrap, network, ADS, scan, online state, CoE/SDO, PLC, controller, or hardware change |
| CMake/qbs execution | Neither description changed, so qbs was not run; the unrelated known EasyBoard test-all blocker remains outside this issue |

Evidence is under
`/private/tmp/embed-labs-wb-nav-blank-context-001.oYIrwR`. Qt's item hit-test
and current-index contracts are at
<https://doc.qt.io/qt-6/qabstractitemview.html#indexAt> and
<https://doc.qt.io/qt-6/qabstractitemview.html#currentIndex-prop>. Beckhoff's
selected Devices/EtherCAT device Add New Item context is referenced at
<https://infosys.beckhoff.com/content/1033/xts_software/11342363403.html> and
<https://infosys.beckhoff.com/content/1033/epioconfiguration/6519655307.html>.

## EtherCATWorkbench General name-rejection feedback qualification

`ISSUE-WB-GENERAL-RENAME-REJECTION-FEEDBACK-001` is qualified from local
baseline `6d13931b6d595b4c710227dc0aa7fa0a4584c104`.

| Qualification | Current evidence |
| --- | --- |
| User-visible defect | Empty Project, offline Target, Master, and configured Slave name submissions restored the accepted value but explained the rejection only in General Messages, leaving the active Details page apparently unresponsive |
| Authority and ordering | Existing Workbench controller and Project-service paths remain authoritative; the page retains the existing contextual error, restores authoritative context, then shows it in a private error `InfoLabel` |
| Rejection invariants | All four accepted names, the complete Project snapshot, Undo/Redo availability, and stable selection remain unchanged; `projectChanged` remains zero |
| Feedback lifecycle | New text clears the state and a successful path remains clear; same-context refresh, context switch, Project close, and page teardown clear or destroy visible text, accessible description, and normal/additional tooltip |
| Accessibility | Stable accessible name, exact description/tooltips, and exactly one optional Polite announcement are verified per rejection; clearing and successful paths emit no extra announcement |
| Failure-first | Production cpp/header retained SHA-256 `51195c26f5db3936f2e0ba0935d2564d3b8a0bf2ca95c86328de7d39e6ee16e6` and `93760569091e384acb8a68451b8c3ae8c2f6ce2f744bbcab700780bc1423aaca`; initialization/cleanup passed, the missing feedback assertion failed, target status was 1 |
| Superseded harness attempts | A typed `findChild<InfoLabel *>` compile failure and malformed `-test` selector were setup errors, not red evidence; a zsh reserved-name wrapper was corrected; a parallel Scan exit-handshake stall was superseded by the final seven-pass status-0 sequential run |
| Focused normal and 2x | Each authoritative run passed 3 events, 0 failed, target status 0 |
| Related normal and 2x | Each authoritative run passed 10 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two authoritative runs at each scale passed 81 events per run, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 81, Scan 7, Diagnostics 7; 132 passed, 0 failed, every authoritative target status 0 |
| Final source SHA-256 | General cpp `ec0c1b04f6600b7e220d2ba49f52211d32fc55e9419daee24453db43bb93bbb6`; header `e2d7e95eb78f39e758afe5efe599627d1e48888bd877e85cd9abbd17936a0026`; tests `af57ecdbd2858beb8cb1d834627b8c8eaa655a67c0621f594b11a8fc27e78dbf`; test header `4352a7df522ecbb2deb147ea15234070b025b7b310cc480807698a5abb08acbb` |
| Final git blobs | General cpp `74b7a4e1daeb01026eb1eb696e4a46d5d7eb20dd`; header `4c77773da4de1e45d5c81ae179ce4e9ce9a368bd`; tests `e431f9014d90eeba9c48b2e915ba502ad434b89e`; test header `3839b41791da3d01e58482dde078e00811e88a03` |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | `WITH_TESTS=OFF` Workbench target and full product passed in `qt-creator-build-ethercat-product-qt611`; exactly 16 plugin dylibs |
| Product hashes | Executable `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `cdc247778382d9ffa7ec655aa1538011d8184bae5ad9403450baa0109562f8e6`; test Workbench `98459d3f9c915105deef5080935ac481a56c31f4bd4d1185c1ee362697d9a1b2` |
| Enabled/disabled lifecycle | Both invisible product runs stayed alive for 37/37 samples; Workbench mapped in 37 enabled and 0 disabled samples; each ended with expected passed-through SIGTERM status 15 |
| Fail-closed crash audit | For 2026-07-23 03:23:45–03:26:10 +0800, log query status/header were explicitly validated, crash-service matches were 0, readable before/after DiagnosticReports lists were 166/166, explicit difference had 0 Embed Labs reports, and residual processes were 0 |
| Invisible executable policy | Fresh HOME/settings, offscreen Qt, disabled crash reporting, `-no-crashcheck`, cleared DYLD variables, and process-local Touch Bar bypass; no visible/manual UI inspection |
| Deliberate exclusions | No public API/role, Project format, Provider/ProjectService change, network, ADS, scan, online CoE/SDO, controller, PLC, or hardware behavior is claimed |
| Local-only policy | No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was performed |
| CMake/qbs execution | Neither build description changed, so qbs was not run |

Evidence is under
`/private/tmp/embed-labs-wb-general-rename-feedback-001.M4aWr4`. Qt's
announcement and accessible-description contracts are at
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html> and
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>. Beckhoff's
Project, Master, and Slave General-name pages are referenced only for
terminology at
<https://infosys.beckhoff.com/content/1033/tc3_userinterface/3434440203.html>,
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1569945995.html>, and
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1341899531.html>.

## EtherCATScan owning-Project lifecycle qualification

`ISSUE-WB-SCAN-PROJECT-LIFECYCLE-001` is qualified from local baseline
`e1855127275d89914626a632674e08cb88109d98`.

| Qualification | Current evidence |
| --- | --- |
| User-visible defect | A terminal local Mock result could outlive its Project, leave Compare/Accept/Keep and the status contribution active, and be projected again after the same stable Project ID reopened |
| Failure-first | Test-only changes left production cpp/header/workflow hashes unchanged; initialization and cleanup passed, owner close left actual state `Completed` (`6`) instead of `Idle` (`0`), and the target exited 1 |
| Ownership gate | `projectAboutToBeRemoved` is matched only against the current `ScanRequest::projectId`; no Project QObject or model index is retained |
| Unrelated Project | Active, Completed, Failed, and Cancelled rows preserve exact progress/result/error, page, command, and status state and emit zero Provider state/result/finished signals when the unrelated Project closes |
| Owning Project | Active first emits `Cancelled` and one `scanFinished(Cancelled)`, then every row reaches `Idle`; result/error/progress and the difference page clear, and Compare/Accept/Keep/Cancel plus the Scan status are inactive |
| Multi-Project boundary | The owner closes while another real Project remains open; the remaining Project is preserved and no scan state transfers to it |
| Same-ID reopen | Reopening the same file does not revive transient scan state; every row completes a new explicit Mock scan and preserves stable Project configuration and empty Undo/Redo history |
| Focused normal and 2x | Each final run passed 6 events, 0 failed, target status 0 |
| Related Scan normal and 2x | Each final run passed 9 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two final runs at each scale passed 81 events per run, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 81, Scan 11, Diagnostics 7; 136 passed, 0 failed, every target status 0 |
| Final source SHA-256 | Provider `61d83c8409f0a9aded41507a77036fb9128307bfde9beafeb14b16b18a8d48ad`; tests `93aac13f59ec72d173b8a7e3dcf32c7fcc8fb6b45295bfd0d7fb3c3d398c07f7`; test header `985582e5552d25464863bc2191b90e7820d1a374e02f28e637f562b2a5b3c6e3` |
| Final git blobs | Provider `e9ea00afa5470abaca43854ffa1057c4537db8c1`; tests `11d5e1ee9e5a89f63bea46fd743068b4bd5caa32`; test header `484c4b973c540e497b4734d33f1033eae09c59d6` |
| Qualified Qt and test build | Qt 6.11.0 Release; `qt-creator-build-ethercat-core-qt611` |
| Product build and inventory | `WITH_TESTS=OFF` Scan target and complete product passed in `qt-creator-build-ethercat-product-qt611`; exactly 16 plugin dylibs |
| Product hashes | Executable `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Scan `e2c4c797283a9cb5f345bc6533214541a1217cf6f5194fc7a422be737699b274`; test Scan `0b4140211077357d0894335af00027604ea7a7bfc539b5ebe2f67e4be5119f63` |
| Enabled/disabled lifecycle | Both invisible product runs stayed alive for 37/37 samples; Workbench and Scan mapped in 37 enabled and 0 disabled samples; each ended with expected passed-through SIGTERM status 15 |
| Fail-closed crash audit | From 2026-07-23 04:14:08–04:16:34 +0800, residual processes, new Embed Labs DiagnosticReports, and matching ReportCrash/CrashReporter/diagnosticd events were all 0 |
| Known non-fatal launch message | The explicit-disabled run emitted the existing shared-memory initialization message, stayed alive for 37/37 samples, and produced no diagnostic report or crash-service event |
| Invisible executable policy | Fresh HOME/settings, offscreen Qt, disabled crash reporting, `-no-crashcheck`, cleared DYLD variables, and process-local Touch Bar bypass; no visible/manual UI inspection |
| Superseded test setup | A private Workbench-symbol link attempt and a cross-reopen full-snapshot comparison affected only test construction and are not counted as failure-first or product failures |
| Mock boundary | No physical interface, controller, network, ADS, SDO, online scan, PLC, Zynq, or hardware behavior was exercised or claimed |
| Local-only policy | No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was performed |
| CMake/qbs | Neither description changed, so qbs was not run |

Evidence is under
`/private/tmp/embed-labs-wb-scan-project-lifecycle-001.9fVQNz`. Qt ownership,
timer, and command-state references are
<https://doc.qt.io/qt-6/qobject.html#connect>,
<https://doc.qt.io/qt-6/qtimer.html#stop>, and
<https://doc.qt.io/qtcreator-extending/actionmanager.html>. Beckhoff's scan
reference supplies workflow terminology only:
<https://infosys.beckhoff.com/content/1033/ps2001-2420-1001/10832129675.html>.

## EtherCATWorkbench Process Data edit-rejection feedback qualification

`ISSUE-WB-PROCESS-DATA-EDIT-REJECTION-FEEDBACK-001` is qualified from local
baseline `037bd681e7ce17bd665d345f83a64b49a28a2428`.

| Qualification | Current evidence |
| --- | --- |
| User-visible defect | Invalid PDO Content text could close its editor, restore the accepted cell, and leave the positive `Configuration is valid` strip without explaining the rejection |
| Local parse guidance | Real editors report exact allowed forms/ranges for Index, Subindex, Bits, Bit Offset, and non-empty Name |
| Domain validation | A real Bits `8` edit against `UINT16` reports the first complete-configuration error with `Change not applied.`; accepted data remains 16 bits |
| Delegate-only boundary | The standard and Type delegates consume the model's private one-shot result; rejected direct programmatic `setData()` publishes no feedback or announcement, both with no editor and while a real editor is open; valid direct writes retain the existing Project path |
| Rejection invariants | Complete Project snapshot, accepted EditRole value, Undo/Redo availability, `projectChanged`, and model `dataChanged` remain unchanged |
| Feedback/accessibility | Error type, stable accessible name, exact accessible description and both tooltips, and one Polite announcement are verified for each real rejection |
| Clearing | PDO Content cell and Sync-Manager selection directly restore normal validation metadata without announcement; successful edit, Undo/Redo, repeated rejection, and Project close are verified; PDO selection uses the same reviewed private clear path |
| Defensive branches | Read-only and Project-service failures return delegate feedback strings by code inspection; service failure injection is deliberately not claimed |
| Failure-first | Test-only changes retained production cpp/header hashes `229c2660bddc89c6bf7d5cafb481fb6a646ec672ea777cc915421aa9402ad955` and `ab7d21d3820117780abc31c232e99b7541fd38138378d4ab2fce4deffdf786ad`; a real blank-Name Return left `InfoLabel::Ok`, and the target exited 1 |
| Superseded test harness | An intermediate raw `QLabel *` Project-close assertion dereferenced a destroyed widget; the final test uses `QPointer` and does not count that run as product evidence |
| Delegate ODR correction | A pre-final related run exposed a real Startup delegate teardown `EXC_BAD_ACCESS`; LLDB/MallocScribble traced two different linker-visible `DataTypeDelegate` classes, and renaming the Process Data class to `ProcessDataTypeDelegate` produced distinct destructor/vtable/typeinfo symbols |
| Post-fix focused normal and 2x | Each run passed 3 events, 0 failed, target status 0 |
| Post-fix related normal and 2x | Each run passed 9 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two post-fix runs at each scale passed 82 events per run, 0 failed, target status 0; Startup editing passed in every run |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 82, Scan 11, Diagnostics 7; 137 passed, 0 failed, every target status 0 |
| Final source SHA-256 | Process Data cpp `00cb248c93611ff911941f2af9632d2f4abbe19a56b8c13ca6a7ac2c75507eb0`; header `a489a2cde89a7a13e993427c2ee342d6d1b4e0342d65618f06c3d5904b12bba4`; tests `37311df512697533f4bdc639a760b80c26479898d1669efcac47599c82d81948`; test header `e0860f1940a699c8c1f53cd0694b5918f7b88792f6fe27e1b08a4cdbdd1c1d70` |
| Final git blobs | Process Data cpp `b38e47a0be15a0ca5c695378b5b1c1bdb5394f84`; header `e436385dc4881c138bad1522037fe58397bc75ea`; tests `d5cc3ec1986c9d685bbeed29e5386914d3f88cf8`; test header `a1ed3404b855a1bf0969546293f559051448622f` |
| Qualified build | Qt 6.11.0 Release tests; `WITH_TESTS=OFF` Workbench target and full product passed with exactly 16 plugin dylibs |
| Product hashes | Executable `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `06e77fefb9451cdae1fd1912e4cb982aeecf2365a2a99c59077d8372b60b2763`; test Workbench `63829550a2be2a9516737c58589af97780640c2d88595d2a0805a6ec2752dd33` |
| Enabled/disabled lifecycle | Both invisible product runs stayed alive for 37/37 samples; Workbench and Scan mapped in 37 enabled and 0 disabled samples; both ended with expected status 15 |
| Crash-dialog audit | From 2026-07-23 05:08:22–05:10:47 +0800, residual processes, new Embed Labs DiagnosticReports, and matching ReportCrash/CrashReporter/diagnosticd events were all 0 |
| Invisible execution | Fresh HOME/settings, offscreen Qt, disabled crash reporting, cleared DYLD variables, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; no visible/manual UI inspection |
| Mock/offline boundary | No physical interface, network, ADS, physical or online scan, online CoE/SDO, controller, PLC, Zynq, or hardware behavior was exercised or claimed; Mock Scan regression tests remain in scope |
| Build descriptions | No CMake or qbs file changed, so qbs was not run |
| Local-only policy | No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was performed |

Evidence is under
`/private/tmp/embed-labs-wb-process-data-edit-feedback-001.seBJey`. Qt model,
delegate, and announcement contracts are at
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>,
<https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData>, and
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. Qt Creator's
local validation precedent is at
<https://code.qt.io/cgit/qt-creator/qt-creator.git/tree/src/libs/utils/projectintropage.cpp?h=20.0#n193>.
Beckhoff's Process Data page supplies PDO terminology only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1344982411.html>.

## EtherCATWorkbench Startup edit-rejection feedback qualification

`ISSUE-WB-STARTUP-INLINE-EDIT-REJECTION-FEEDBACK-001` is qualified from
local baseline `79b654f5cd57116e42777bb2f09559b87bc98c52`.

| Qualification | Current evidence |
| --- | --- |
| User-visible defect | A real Startup Data `0G` submission restored `08` but incorrectly said that an even hexadecimal digit count was required |
| Real-editor coverage | Data non-hex, odd-digit, and empty candidates; Transition angle brackets; Order, Index, and Subindex ranges; Type/raw-size mismatch; zero Index; and Enabled/empty-Data rejection are submitted through real editors or the real checkbox delegate event |
| Delegate-only boundary | Normal, Transition, and Type `setModelData()` paths and Enabled `editorEvent()` consume a private one-shot model result; rejected direct programmatic `setData()` remains presentation-silent, while valid direct writes retain the Project path |
| Rejection invariants | Accepted EditRole/CheckState values, complete Project snapshot, Undo/Redo availability, exact current cell, `projectChanged`, and model `dataChanged` remain unchanged |
| Feedback/accessibility | Error type, stable accessible name, exact visible text, accessible description, and both tooltips are verified for each real rejection; when Qt accessibility is enabled, one Polite announcement request is also verified |
| Clearing and teardown | Changing cells, successful same-cell submission, Undo/Redo, same-Project refresh, and Project close restore or destroy the full feedback surface without an extra announcement |
| Deliberate exclusion | Modal New/Edit `StartupParameterDialog` feedback is outside this table-inline issue; no manual VoiceOver, audible speech, or visible desktop run is claimed |
| Failure-first | Test-only changes retained production cpp/header SHA-256 `99902dbe8615dedc25380b8bbb7117ad54da9d092ec3e12bcf97afba57c5235f` and `01c85b28ca3b487f425c03380e4f1e6662574512a377c05344cd6157093e6eb6`; initialization/cleanup passed, the exact message assertion failed, and the target exited 1 |
| Focused normal and 2x | Each final run passed 3 events, 0 failed, target status 0 |
| Startup-related normal and 2x | Each final run passed 9 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two final runs at each scale passed 83 events per run, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 83, Scan 11, Diagnostics 7; 138 passed, 0 failed, every target status 0 |
| Final source SHA-256 | Startup cpp `5d6bc7c41dfa8b3cef0925fba090d27960cdd44a876bb666c69ff390b894731e`; header `2c47a50f70ac55ae3a709afe38e9ca2d63e310a6eaffb1ced9fa39463a9bfdc0`; tests `87d3c47b3f7bbf85fc1cdc724d70c4ffe62bb98cc9d333b9b41db1164bcd0fcc`; test header `ab61a8eae20015b5a80d2bc832f0f27a1f05edc9d4ede201f1524f3d298bb28f` |
| Final git blobs | Startup cpp `e7c4d58d6d8fb737363444a4daff4e78f0c9f069`; header `7e7b56533ad09b3c2a70cc5423aa69f1e85c26f7`; tests `5cf94b7853ea7f710aeb9690666d4a37e9010fe2`; test header `8e28aa00ce6d2cbbb71b74b20e788bd56610c3b2` |
| Qualified build | Qt 6.11.0 Release tests; `WITH_TESTS=OFF` Workbench target and full product passed with exactly 16 plugin dylibs |
| Product hashes | Executable `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `5dbc4a74f2730d496068f26806ef1c2ff53dd7a14bb1bbd224ec327db84b48aa`; test Workbench `a9c276d76b51e942e6d5fa5e4faed72f30e8406b89a0340abface59698ff147a` |
| Enabled/disabled lifecycle | Both invisible product runs stayed alive for 37/37 samples; Workbench mapped in 37 enabled and 0 explicit-disabled samples; both ended with expected passed-through SIGTERM status 15 |
| Crash-dialog audit | From 2026-07-23 06:23:25–06:25:48 +0800, residual processes, new Embed Labs DiagnosticReports, and matching ReportCrash/CrashReporter/diagnosticd events were all 0 |
| Final-artifact binding | Pre/post lifecycle SHA-256, mtime, size, and Mach-O UUID manifests are identical and bind both 37-sample runs to the listed final executable and Workbench dylib |
| Invisible execution | Fresh HOME/settings, offscreen Qt, disabled crash reporting, cleared DYLD variables, `-no-crashcheck`, process-local Touch Bar bypass, and passed-through SIGTERM; the workflow was not deferred and no visible/manual UI inspection was run |
| Mock/offline boundary | Mock Scan, Mock online-state, and Mock CoE regressions remain in scope; no physical interface, network, ADS, real scan, real online CoE/SDO, controller, PLC, Zynq, or hardware behavior was exercised or claimed |
| Public/API/system boundary | Private Startup page/model/delegate only; no public API/role, source file, dependency, Project format, persistence, Provider/ProjectService contract, Project command, Core/ProjectExplorer hook, application bootstrap, or production thread/timer change |
| Build descriptions | No CMake or qbs file changed, so qbs was not run |
| Local-only policy | No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was performed |

Evidence is under
`/private/tmp/embed-labs-wb-startup-edit-feedback-001.QUiKKW`. Qt model,
delegate, and announcement contracts are at
<https://doc.qt.io/qt-6/qabstractitemmodel.html#setData>,
<https://doc.qt.io/qt-6/qstyleditemdelegate.html#setModelData>, and
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. Beckhoff's
Startup page supplies ordered-request and fixed-item terminology only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1345265931.html>.

## EtherCATWorkbench Distributed Clocks edit-rejection qualification

`ISSUE-WB-DC-EDIT-REJECTION-FEEDBACK-001` is qualified from local baseline
`cf697da20467541b7d01e636e8ae2267969ae24a`.

| Qualification | Current evidence |
| --- | --- |
| User-visible defect | A rejected DC edit restored the accepted value and showed a reason, but the reason was incomplete on tooltip/accessibility surfaces and remained while the user began correcting the input |
| Rejection paths | Real editors cover invalid AssignActivate, empty enabled Operation Mode, out-of-cycle SYNC0 shift, and a combined SYNC1-without-SYNC0 plus invalid-cycle candidate; the real SYNC0 checkbox covers the disabled-DC domain rejection |
| Rejection invariants | Complete Project snapshot, Undo/Redo availability, stable selection, and `projectChanged` count remain unchanged; rejected widgets restore accepted values |
| Feedback/accessibility | Error type, stable accessible name, exact visible text, accessible description, both tooltips, and one conditional Polite announcement are verified for every covered rejection; the two-error case keeps both reasons outside the concise visible summary |
| Clearing and teardown | User correction clears before Return; accepted submission, Undo/Redo, DC enable recovery, same-Project refresh, and Project close restore or destroy the complete feedback surface without an extra announcement |
| Failure-first | Production cpp/header SHA-256 stayed `db028f1b2dd3d4b7a294c0ae7746e27b7e5d3668987d153f4181b0d671f3c916` and `94ea851d21149b5ce52a07ecfa23ebc7cb0badc145691ae52cf3447bb4002a46`; init/cleanup passed, the accessible name was empty, one test failed, target status 1 |
| Focused normal and 2x | Each final run passed 3 events, 0 failed, target status 0 |
| DC-related normal and 2x | Each final run passed 7 events, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two final runs at each scale passed 84 events per run, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 84, Scan 11, Diagnostics 7; 139 passed, 0 failed in six isolated LLDB-supervised processes |
| Staged independent review | One P2 found that the initial implementation overwrote multi-error tooltip details with the first visible reason; the final implementation and a real two-error test retain every reason, then all affected gates were rerun |
| Final source SHA-256 | DC cpp `4863554223909269403b1c52807ec1102b70dcebc45f3dfc4e6f4a77c8289dcf`; header `f99a9385ff826307176132a1a20623b356ef4dd3ade59e57de505af23b0871ee`; tests `97842bb3d4e6886faf52a12b54bfcb1f5fac8f3b2bbe2531e362f2d81f853495`; test header `9d369afd216e5e31bdd8826d56b541dfa8982407571c66f5f2a1b54da9f7141d` |
| Final git blobs | DC cpp `9de30c17480490200b0a4eef8a8b991ee4834603`; header `5e79d96152ba57ed563a7bb81730fb434667a656`; tests `21c4a2ec5901362a1e1e64e1b2b8c13c296c54e2`; test header `67d6c88d6afd707fbcf3b799ce56ad807a9aae6b` |
| Qualified build | Qt 6.11.0 Release tests; `WITH_TESTS=OFF` Workbench target and complete product passed with exactly 16 plugin dylibs |
| Product hashes | Executable `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `5d22599750f502b408b7d9924b6c4f297570484f1a5d763f48c885d04f3b09ba`; test Workbench `83266a924f762747cd21571c0c2c8679fb5cf18410adb325829f6cae98e0c319` |
| Enabled/disabled lifecycle | Both invisible product runs stayed alive for 37/37 samples; Workbench mapped in 37 enabled and 0 disabled samples; each ended with expected status 15 |
| Crash-dialog audit | From 2026-07-23 07:17:15–07:19:37 +0800, residual processes, new Embed Labs DiagnosticReports, and matching crash-service events were all 0 |
| Final-artifact binding | Pre/post SHA-256, mtime, size, and Mach-O UUID manifests are identical |
| Deliberate exclusions | No visible/manual UI, manual VoiceOver, audible speech, physical interface, network, ADS, real scan, online CoE/SDO, controller, PLC, Zynq, or hardware behavior is claimed |
| Public/API/system boundary | Private DC page and Workbench tests only; no public API/role, source list, dependency, Project format, persistence, Provider/ProjectService contract, Project command, Core/ProjectExplorer hook, or application bootstrap change |
| Build descriptions | No CMake or qbs file changed, so qbs was not run |
| Local-only policy | No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was performed |

Evidence is under
`/private/tmp/embed-labs-wb-dc-edit-feedback-001.SmAI4Q`. Qt contracts are at
<https://doc.qt.io/qt-6/qlineedit.html#editingFinished>,
<https://doc.qt.io/qt-6/qwidget.html#accessibleDescription-prop>, and
<https://doc.qt.io/qt-6/qaccessibleannouncementevent.html>. Beckhoff's DC
reference supplies terminology only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1358002571.html>.

## EtherCATWorkbench native navigation Find qualification

`ISSUE-WB-NAV-NATIVE-FIND-001` is qualified from local baseline
`9d33e6535bcb140b1fb0e2d2b66e61e7797c3bf6`.

| Qualification | Current evidence |
| --- | --- |
| User-visible gap | The left tree had a permanent recursive Filter but no aggregated Qt Creator `IFindSupport`, so standard current-item Find could not attach |
| Native integration | The existing tree is wrapped by `Core::ItemViewFind`; automation focuses the tree, triggers `Find.FindInCurrentDocument`, and verifies the visible `findEdit` is attached to this wrapper; command, toolbar, Next/Previous, and platform shortcut remain Core-owned |
| Find versus Filter | Filter still searches private Name, Status, identity, and presentation details and hides nonmatching rows; Find walks visible `Qt::DisplayRole` Name/Status cells in the current proxy projection and never changes filter text |
| Selection boundary | A match changes the tree current index and propagates only its stable `NodeId` through the existing `SelectionService`; no Project activation, snapshot mutation, Undo command, ESI import, or replacement occurs |
| Refresh lifecycle | Model reset, row insertion/removal/movement, and layout change reset the incremental anchor; a three-device move test distinguishes the reset start point from a stale ordinary `QModelIndex` and retains stable `NodeId` selection |
| Ownership and cleanup | Standard aggregation owns the finder/tree relationship; `QPointer` coverage proves both finder and tree are destroyed with the navigation widget |
| Empty/error behavior | A filtered-out visible name returns `NotFound`, preserves the filter and selection, and leaves Project context unchanged; the existing no-match Filter page remains separate |
| Core flags | Backward and case-sensitive operation is exercised; backward, case-sensitive, and regex flags remain available; replace is unsupported |
| Deliberate Core exclusion | A superseded test-only multi-token whole-word assertion returned `NotFound`; no direct Core patch was authorized, so the private Workbench finder suppresses the broken Whole Words flag and the final test verifies its absence |
| Failure-first | Test-only changes retained production navigation hashes `be3fb065bc0b9351314eb98ac4e315482159a28e6527d121c1958fc398479da3` and `e874a45f55f16a8bd5f1db9113eaaadfc96c36c6ea690bf99fc0740219e45ffc`; missing `IFindSupport` failed and target status was 1 |
| Focused normal and 2x | Each final run passed 3 events, 0 failed, target status 0 |
| Navigation-related normal and 2x | Ten tests plus init/cleanup passed 12 events per run, 0 failed, target status 0 |
| Complete Workbench normal and 2x | Two runs at each scale passed 85 events per run, 0 failed, target status 0 |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 85, Scan 11, Diagnostics 7; 140 passed, 0 failed, every target status 0 |
| Diagnostics teardown audit | One initial concurrent batch emitted two timer-thread warnings after Diagnostics passed; two isolated reruns and the final sequential six-suite run each passed 7 events with zero warnings, so final lifecycle evidence uses the clean sequential run |
| Final source SHA-256 | Navigation cpp `0ed26fa0c08eff697bad8fbc44bfe406721281d538010eaca9901e85be55356e`; header `f372c045c8e8cb8117060a15129f5c0dbfe3acaea5c071e636c2ef4ebd469ef5`; tests `90bf459eae7c2e56863d24a983bd121cc7754ddbeafb9fed5b5c12c2a63af06d`; test header `f6d3a61377d8e3fae185b61f2ffdf1fe8513dfe24451d1d3be25eb34a8a55da9` |
| Final git blobs | Navigation cpp `90e46c9b9b58aabce0a2cac7061d3023f5cd6979`; header `fe7d693279e2a169e89c5b750a5d733fde3c0022`; tests `6e0142fbdb45e27cd4b3d5a56a9833a9a71860da`; test header `90465c5a81b9fdb6a6483278515b3e5fa6ef1f9c` |
| Qualified build | Qt 6.11.0 Release tests; `WITH_TESTS=OFF` complete product passed with exactly 16 plugin dylibs |
| Product hashes | Executable `c6f36b6a3f01cd97b59dc82a4a1ccd420be3939311cf59ebd9b5f11b767cb4db`; product Workbench `c89ce5c0cfb64697a4996ccef3a5bbc279d6ab5014a38c9db06c5c68a80fa97b`; test Workbench `74259a3cb7fcc54070f2167ff38e5f806582c3d3ac959debbf60f598a115e787` |
| Enabled/disabled lifecycle | Both invisible product starts stayed alive 37/37 samples; Workbench mapped 37/37 enabled and 0/37 disabled; each ended with expected passed-through SIGTERM status 15 |
| Crash-dialog audit | From 2026-07-23 08:01:22–08:03:51 +0800, residual processes, new Embed Labs DiagnosticReports, and matching crash-service events were all 0; product artifacts were unchanged |
| Deliberate exclusions | No visible/manual Find-toolbar or VoiceOver check, physical interface, network, ADS, real scan, online CoE/SDO, controller, PLC, Zynq, or hardware execution is claimed |
| Public/build boundary | Private Workbench cpp/header and existing tests only; no public API/role, Core/ProjectExplorer patch, dependency, source list, Project format, Provider contract, CMake, or qbs change |
| Local-only policy | No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was performed |

Evidence is under
`/private/tmp/embed-labs-wb-nav-native-find-001.i4nnnB`. Qt Creator's Find and
Find-versus-Filter references are
<https://doc.qt.io/qtcreator/creator-editor-finding.html> and
<https://doc.qt.io/qtcreator/creator-how-to-view-output.html>. The local API is
<https://code.qt.io/cgit/qt-creator/qt-creator.git/tree/src/plugins/coreplugin/find/itemviewfind.h?h=20.0>.
Beckhoff's tree workflow reference supplies terminology only:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html>.

## EtherCAT bilingual and compact-navigation qualification

`ISSUE-WB-I18N-COMPACT-NAV-001` is qualified from local baseline
`2af6b18704b13f9b2cb2a06af18b4dfcfb658c7d`.

| Qualification | Current evidence |
| --- | --- |
| Language selection | Reuses Qt Creator's existing `General/OverrideLanguage` setting at `Preferences > Environment > Interface > Language`; English is source text, Simplified Chinese is `qtcreator_zh_CN`, and the existing restart prompt applies |
| EtherCAT Chinese coverage | Six contexts and 1249 current unique source messages; 0 missing, extra, unfinished, empty, obsolete, or placeholder-mismatched messages |
| Scope of language claim | Complete for product-owned EtherCAT contexts only; unrelated upstream Qt Creator contexts retain their existing partial translation state |
| Compact visible state | Status-column `DisplayRole` is concise; Name and Status columns stretch inside the viewport and use `ElideRight`, with no horizontal range in the narrow long-Chinese widget case |
| Complete-value recovery | `StatusRole`, status-cell `AccessibleTextRole`, tooltip, accessible description, device identity, Provider details, and recovery guidance retain complete text |
| Filter and native Find | Both consume column-zero `SearchTextRole`, which combines name, full and compact status, identity, and detail-only text; Find remains limited to the visible proxy projection |
| Stable behavior | Search selection still publishes only stable `NodeId`; no Project mutation, activation, persistence, Undo command, ESI import, or Provider ownership changes |
| Failure-first | The focused test failed on the old full status `DisplayRole` and `ElideNone` behavior before production changes |
| Focused normal and 2x | Ten tests plus init/cleanup passed 12 events at each scale; the final full/compact/details Find selector passed 4 events at each scale |
| Complete Workbench | 85 passed, 0 failed |
| Six-plugin regression | Core 17, Project 12, Devices 8, Workbench 85, Scan 11, Diagnostics 7; 140 passed, 0 failed |
| Translation gates | `lupdate` found 1249 existing and 0 new strings; XML, placeholder, empty-value, `lrelease`, `.qm` round-trip, and six-context/message-count checks passed |
| Qualified product build | Qt 6.11.0 Release, `WITH_TESTS=OFF`, complete build passed with exactly 16 plugin dylibs |
| Current Chinese artifact | `qtcreator_zh_CN.qm` contains all 1249 EtherCAT messages; SHA-256 `3b93ff1ef832ad55676db250f44e2272ec63df58b77f1ad6ddb4485cef5a0e52` |
| Deliberate runtime exclusion | At the user's explicit request the normal product was not launched; no visible language-switch or current-artifact enabled/disabled lifecycle smoke is claimed |
| Test isolation | Executable tests used offscreen Qt, fresh HOME/settings, disabled crash reporting, cleared DYLD variables, `-no-crashcheck`, and LLDB supervision |
| Public/API/system boundary | Existing shared TS catalogue plus private Workbench tree/navigation/tests and four documents only; no Core/ProjectExplorer/app change, public API, Project format, Provider contract, thread, timer, network, ADS, controller, PLC, Zynq, or hardware behavior |
| Build descriptions | No CMake or qbs file changed; no paired source-list update was required and qbs was not run |
| Local-only policy | No remote comparison, fetch, pull, merge, rebase, push, PR, or publication was performed |

Evidence is under `/private/tmp/embed-labs-i18n-compact`. Beckhoff's tree
references provide hierarchy terminology only; the bilingual selector and
compact rendering are Qt Creator-native product behavior:
<https://infosys.beckhoff.com/content/1033/tc3_io_intro/1084406539.html>.

## EtherCAT controller connection Core/API qualification

`ISSUE-CORE-CONTROLLER-CONNECTION-API-001` is qualified from local baseline
`06538be209e26ff0b8a4b8ad4f231a7d15c0631b`.

| Qualification | Current evidence |
|---|---|
| Failure-first | The Core target failed first because the contract test required the intentionally absent `ethercatdata/controllerconnection.h` |
| Initial public scope | Stable Project/Master request, fixed endpoint/three-channel session snapshot, controller/capability/package/firmware summaries, structured error, and `ControllerConnectionProvider`; the Provider/Profile revision below replaces the fixed transport assumptions |
| Existing enum compatibility | `ProviderKind::ControllerConnection` is appended as value 5; prior Provider kinds are unchanged |
| Invalid requests | Empty Project ID, Master ID, host, and each zero port are rejected without a snapshot notification |
| Lifecycle | Disconnected, Connecting, Connected, Degraded, Failed, refresh, idempotent disconnect, reconnect, and connection-generation invalidation passed |
| Channel/session evidence | Control 4,096-byte limit; Push/Bulk 65,536-byte limits; negotiated protocol, SessionId, BootId, lease observation, heartbeat, and all channel roles retain value semantics |
| Error attribution | Local Protocol errors do not contain controller status/operation results; explicit controller-returned errors preserve those fields separately |
| Provider registry | New kind registers, filters, unlinks, and removes through the unchanged generic registry |
| Focused Core suite | EtherCATCore 18 passed, 0 failed |
| Sequential six-plugin regression | Core 18, Project 12, Devices 8, Workbench 85, Scan 11, Diagnostics 7; 141 passed, 0 failed |
| Test isolation | Qt 6.11.0 Release, fresh HOME/settings, offscreen Qt, disabled crash reporting, cleared inherited DYLD variables, `-no-crashcheck`, and LLDB Touch Bar bypass |
| Product build | `WITH_TESTS=OFF` complete product passed with exactly 16 plugin dylibs |
| Enabled lifecycle | Product stayed alive for 10/10 samples with `libEtherCATCore.dylib` mapped in 10/10; intentional SIGTERM returned target status 15 |
| Disabled lifecycle | Product stayed alive for 10/10 samples with `-noload EtherCATCore` and Core mapped in 0/10; intentional SIGTERM returned target status 15 |
| Cleanup/crash boundary | Zero matching residual qualification processes and no new Embed Labs DiagnosticReports in the final audit window |
| CMake/qbs | New Data header is listed in both; qbs execution not run because the executable remains unavailable |
| Dependency boundary | EtherCATData remains Qt Core-only; EtherCATCore adds no direct Qt Network dependency, socket, codec, or worker |
| Upstream boundary | No Qt Creator Core, ProjectExplorer, or application-bootstrap change; direct Core intrusion count remains five |
| Hardware claim | None; the fake Provider qualifies only the in-process API, while Python read-only results remain separate protocol input |
| Local-only policy | No fetch, pull, merge, rebase, branch switch, push, PR, or publication |

The controller endpoint, authoritative Windows protocol hashes, read-only
hardware observations, discovery safety gate, CODESYS workflow mapping, and
client/master issue handoff are documented in
`docs/ethercat-online-controller.md`. Test evidence is under
`/private/tmp/embed-labs-i18n-compact/online-core-api`; lifecycle logs use the
`/private/tmp/embed-labs-controller-core2-*` prefix.

## Multi-vendor controller Provider/Profile qualification

`ISSUE-CORE-CONTROLLER-PROVIDER-PROFILE-002` is qualified from local baseline
`a6bcd2dfe813e16ccc62fd61afa6b41473b6542e`.

| Qualification | Current evidence |
|---|---|
| Failure-first | The Core target failed first on the intentionally absent scope/profile values, profile query, profile signal, and arbitrary channel fields |
| Public scope | Provider-owned profiles, stable Project/Master scope, profile-ID connect request, redacted endpoint summary, and arbitrary named channels |
| Multiple vendors | Two fake connection Providers coexist under distinct global Provider IDs and safely reuse the same Provider-scoped profile ID |
| Profile validity | Null scope/profile IDs, unknown local IDs, configured=false, supported=false, and unavailable Providers are rejected without starting a connection |
| Availability cleanup | An unavailable Provider rejects Connect/Refresh but still accepts Disconnect to complete shutdown cleanup |
| Secret boundary | Every outward-facing profile, channel, and error string must exclude credentials, tokens, certificates, private keys, and raw secret-bearing transport errors |
| Transport neutrality | A second fake Provider exposes one `ipc://controller-1` channel instead of Product API's three TCP channels |
| Removal isolation | Removing one connection Provider leaves the other discoverable; removal of the second empties only that Provider kind |
| Focused Core suite | EtherCATCore 18 passed, 0 failed |
| Sequential six-plugin regression | Core 18, Project 12, Devices 8, Workbench 85, Scan 11, Diagnostics 7; 141 passed, 0 failed |
| Product build | Qt 6.11.0 Release, `WITH_TESTS=OFF`, complete build passed with exactly 16 plugin dylibs |
| Enabled lifecycle | Product stayed alive for 10/10 offscreen samples with `libEtherCATCore.dylib` mapped in 10/10; intentional SIGTERM returned status 15 |
| Disabled lifecycle | Product stayed alive for 10/10 offscreen samples with `-noload EtherCATCore` and Core mapped in 0/10; intentional SIGTERM returned status 15 |
| Cleanup/crash boundary | Zero matching residual qualification processes and no new Embed Labs DiagnosticReports; the existing visible product process was untouched |
| Concrete adapter | Not part of this Core revision; the later independent `EtherCATProductApi` issue supplies the first headless Embed Labs Provider |
| CMake/qbs | No source list or dependency changed; paired build descriptions remain synchronized and qbs was not run |
| Hardware claim | None; no socket, controller connection, bus operation, PLC, Zynq, or hardware access occurred |
| Local-only policy | No fetch, pull, merge, rebase, branch switch, push, PR, or publication |

Evidence is under
`/private/tmp/embed-labs-i18n-compact/controller-profile-v2`.

## Historical headless Embed Labs ProductApi adapter boundary

`ISSUE-ONLINE-PRODUCTAPI-READONLY-ADAPTER-001` supplies the first concrete
connection Provider without changing the generic multi-vendor contract.
This table records the original read-only issue and its dated evidence; the
current lifecycle contract below supersedes its control exclusions.

| Compatibility boundary | Historical issue status |
|---|---|
| Plugin ownership | Product API v1.9 framing, endpoints, Control/Push/Bulk sockets, status mapping, and resume policy remain private to `EtherCATProductApi` |
| Multi-vendor extension | Another manufacturer or incompatible protocol adds an independent plugin and Provider ID; Core and Workbench gain no vendor switch or fixed-channel assumption |
| Generic request | The cross-plugin request remains `{Project/Master scope, provider-owned profile ID}`; it contains no host, port, credentials, role enum, or numeric Product API message |
| Strict outbound allow-list | HELLO, GetState, GetCapability, GetPackageState, capability-gated GetFirmwareState, and feature-gated ResumeEvents only |
| Explicit exclusions | No AcquireControl, Control Heartbeat, discovery, SDO/PDO, runtime transition, package mutation, firmware mutation, scan, or offline Project mutation |
| Auxiliary recovery | Loss of Push or Bulk invalidates the local generation, closes Control/Push/Bulk, and attempts a bounded resume of the complete session while retaining a still-valid alarm checkpoint only for the same SessionId/BootId; replay rejection or a live sequence gap clears it |
| Provider neutrality | Full three-channel resume is ProductApi-private; another Provider may use another channel topology or no session identity |
| Validation evidence | Focused ProductApi suite passed 36 events; sequential seven-suite regression passed 177 events; the Qt 6.11.0 product build passed with exactly 17 plugin dylibs |
| Hardware evidence | This adapter issue's original qualification was local only; the later 2026-07-24 Workbench Communication acceptance separately verified the Qt Provider against the real controller |

Final local qualification for
`ISSUE-ONLINE-PRODUCTAPI-READONLY-ADAPTER-001`:

| Gate | Result |
|---|---|
| Failure-first | Focused target failed on the intentionally missing private codec header before implementation |
| Focused behavior | ProductApi loopback suite passed 36 events with zero failures or skips, including retained same-session and cleared invalid alarm checkpoints |
| Regression | Core, Project, Devices, Workbench, Scan, Diagnostics, and ProductApi passed 177 sequential events with zero failures |
| Translation | Seven EtherCAT contexts contain 1,301 messages with no unfinished or empty translations; ProductApi contributes 52 messages and `lrelease` succeeds |
| Product build | Qt 6.11.0 configured build, `WITH_TESTS=OFF`, completed with exactly 17 plugin dylibs; `CMAKE_BUILD_TYPE` is unset, so this is not labeled a Release build |
| Enabled lifecycle | Product stayed alive for 10/10 offscreen samples with `libEtherCATProductApi.dylib` mapped in 10/10 |
| Disabled lifecycle | Product stayed alive for 10/10 offscreen samples with `-noload EtherCATProductApi` and the plugin mapped in 0/10 |
| Cleanup/crash boundary | Both runs ended with intentional SIGTERM status 15, zero matching residual processes, and no new Embed Labs DiagnosticReports |
| CMake/qbs | Target, sources, dependencies, and plugin-profile entries are synchronized; qbs was not available to run |
| Hardware claim | None; no real controller socket, bus, PLC, Zynq, or hardware operation occurred |
| Local-only policy | No fetch, pull, merge, rebase, branch switch, push, PR, or publication |

Focused evidence is under
`/private/tmp/embed-labs-productapi-api014-focused-final.6WO1vp`; sequential
evidence is under
`/private/tmp/embed-labs-productapi-api014-regression.7JluHU`; final
enabled/disabled samples, mapping checks, exit status, crash-report diff, and
residual-process audit are under
`/private/tmp/embed-labs-productapi-api014-lifecycle.m87DYb`.

## Historical Workbench controller Communication page boundary

`ISSUE-WORKBENCH-CONTROLLER-COMMUNICATION-001` consumes the qualified
connection API and headless adapter without widening either contract.
The table below is the dated read-only issue record. It is retained as
historical evidence and was not rerun for the current automatic-Acquire and
quick-control contract.

| Compatibility boundary | Current issue scope |
|---|---|
| UI integration | One provider-neutral Communication page in the existing right-side Master Details host; no separate controller window |
| Provider choice | Explicit Provider and provider-owned profile selection for the selected Project/Master; no first-provider or cross-vendor fallback |
| Shared commands | ActionManager Connect, Refresh, and Disconnect are shared by the page, EtherCAT menu, and compact controller strip |
| Read-only behavior | Connect and Refresh can request only operations already exposed by `ControllerConnectionProvider`; Workbench has no arbitrary Product API message path |
| Snapshot display | Provider availability, redacted profile endpoint, connection/channel/session state, read-only controller summaries, freshness, and structured errors |
| No hidden lifecycle | Application startup, Project open, Master selection, page open/close, Details navigation, and left-tree navigation do not connect or disconnect |
| Explicit exclusions | No Scan, configuration/Apply, state transition, FreeRun, DC mode, Run, Stop, lease heartbeat, discovery, ECPKG, SDO/PDO, or real diagnostics |
| Existing Mock behavior | Scan and Diagnostics retain their existing Mock Providers and are not rebound by a controller connection |
| Persistence | `{providerId, profileId}` remains Workbench session state; no Project format change |
| Automatic cleanup | On Project close, Workbench clears that Project's in-memory controller selections and requests Disconnect from every Provider whose current snapshot belongs to it and is not already Disconnected or Disconnecting; unrelated Projects are untouched |
| Rejected cleanup | A Project-close cleanup performs no more than five total Disconnect attempts after rejection; every attempt remains bound to the Provider registration epoch, exact scope, and `sessionGeneration` captured for that cleanup |
| Stale retry rejection | Provider removal/re-registration, scope replacement, or `sessionGeneration` change invalidates the pending retry instead of disconnecting a newer or different session |
| Manual cleanup | If all five attempts are rejected, automatic cleanup stops and reports how to recover; from any open EtherCAT Master the user may explicitly select the residual adapter and invoke Disconnect |
| Residual presentation | A residual Failed session remains visible with cleanup guidance instead of being presented as a completed disconnect |
| Focused behavior | Communication-focused tests passed 6/6 at normal scale and 6/6 at 2x scale |
| Workbench regression | Complete EtherCATWorkbench suite passed 89/89 |
| Other isolated suites | Core, Project, Devices, Scan, Diagnostics, and ProductApi passed 92/92 |
| Isolated product regression | Workbench 89/89 plus the other six suites 92/92 passed 181/181 in isolated sequential runs |
| Product acceptance | The Qt 6.11.0 `WITH_TESTS=OFF` product build and enabled/explicitly-disabled Workbench lifecycle checks passed |
| Simplified Chinese | `qtcreator_zh_CN.qm` generation succeeded and every newly added Communication source string passed the non-empty Simplified Chinese translation check |
| Network prerequisite | Control, Push, and Bulk TCP endpoints were reachable; reachability proves only the network path, not a Qt Product API session |
| Hardware selection | On 2026-07-24 the UI selected Provider `Embed Labs Product API`, Profile `v1.9`, endpoint `192.168.3.101:15200` |
| Connect evidence | Connect succeeded with protocol v1.9, `sessionGeneration=1`, `sessionId=10990663912902164094`, and `bootId=5715996203977591977` |
| Read-only snapshot | No lease was held and owner was 0; controller was `SHUTDOWN`/关停 and ready; WKC was 0/0, DC lock and OP were false, and fault was `0x0` |
| Channel evidence | Control, Push, and Bulk were all Connected with reported limits 4,096, 65,536, and 65,536 bytes |
| Refresh evidence | Explicit Refresh succeeded and the displayed update time changed to `12:18` |
| Disconnect evidence | Explicit Disconnect succeeded and Control, Push, and Bulk all became Disconnected |
| Process acceptance | The product exited with status 0; no matching residual process or new Embed Labs DiagnosticReports file remained |
| Safety boundary | No Scan, configuration write, state transition, FreeRun, DC mode, Run, or Stop was invoked |
| Status-bar correction | The client now projects Provider connection state into the unified status control; automated regression covers Connected, Degraded, and Disconnect-to-Offline presentation |
| Secondary hardware UI revalidation | Passed on 2026-07-24; the status bar showed Handshaking, then `Embed Labs Product API — Connected` with real-controller read-only evidence, and returned to Disconnected after explicit Disconnect |

## Current Product API and Workbench lifecycle contract

This section supersedes the read-only adapter/Communication exclusions above
for the current local control extension. It does not change or requalify the
historical results of those earlier issues.

| Compatibility boundary | Current local status |
|---|---|
| Provider neutrality | `ControllerConnectionProvider` exposes typed command support and execution; the default implementation rejects control, so another vendor opts in per command |
| Semantic snapshot | Control request/progress and linear actual-topology values contain no Product API frame, numeric message type, socket, host, or port |
| Protocol negotiation | Current local client contract is Product API v1.10; explicit timing-mode start requires feature bit 11 and the complete `0xfff` feature mask. Negotiated v1.9/`0x7ff` remains bounded compatibility |
| Product API requests | The private adapter adds Acquire, Release, lease Heartbeat, Configuration, DiscoverTopology, RestoreActivePackage, Start, Pause, Resume, ControlledStop, and v1.10 StartFreeRun/StartDc protocol capabilities. Workbench uses generic Start and does not expose explicit timing-mode buttons |
| Communication page | Provider/profile, Connect/Refresh/Disconnect, automatic/manual Acquire, Configuration, Scan Bus, Restore Package, Release, progress, Actual Bus, and authoritative information; no FreeRun, DC Run, Pause, Resume, or Stop buttons |
| Automatic Acquire | After the authoritative connected snapshot identifies the exact Provider/profile/scope/session generation, Workbench queues one Acquire attempt; the page button is manual recovery |
| Lease arbitration | Exactly one session owns control. Other API sessions retain read-only access, while their writes are rejected with `LEASE_BUSY (-10)` |
| Native quick controls | Run maps `OP_SAFE` to Start and `PAUSED` to Resume; Debug maps `RUNNING` to Pause and `PAUSED` to Resume; Controlled Stop is beside them and applies in `RUNNING` or `PAUSED` |
| Full workflow | Connect, automatic/manual Acquire, Configuration, Scan, Restore, native Run, optional Debug Pause/Resume, Controlled Stop, Configuration, Release, Disconnect |
| Discovery safety | Requires owned lease, ready `SHUTDOWN`, package summary present, and package not Active; Actual Bus remains separate from the offline Project |
| Startup safety | Generic Run requires ready `OP_SAFE`, current-Boot active package, operational OP bus, nonzero matching WKC, and zero current/latched faults |
| Recovery | Controlled Stop confirms `OP_SAFE`; the second Configuration confirms `SHUTDOWN` and inactive package before Release |
| Disconnect | Running/Paused Disconnect is rejected; a non-running owned lease is released before channel teardown |
| FreeRun/DC | Workbench Start runs the active package, whose validated ECFG/DC content determines FreeRun or DC. Explicit StartFreeRun/StartDc remain adapter protocol capabilities, not UI buttons; DC lock remains observation |
| Mode mismatch | An external explicit-mode request may receive terminal stage-2 `TIMING_MODE_MISMATCH (-35)` with `(requested_mode << 32) \| actual_mode`; this is not a Workbench timing-mode selection |
| Package boundary | Exact persistent restore and a headless prebuilt-ECPKG upload/validate/optional-activate API are supported; construction/signing, Workbench invocation, ECFG/DC editing, and offline-project package generation remain absent |
| Topology boundary | Position and device identity support truthful linear display; physical port-to-port edges remain unavailable |
| Mock visibility | Production hides local Mock Scan/Diagnostics UI by default; it is available only with `WITH_TESTS` or `QTC_ETHER_CAT_ENABLE_MOCK_UI=1` |
| Current English regression | Workbench 95, Project 15, Devices 8, Core 19, Scan 11, Diagnostics 7, and ProductApi 71; 226 passed, 0 failed, and 1 ProductApi hardware test skipped |
| Current product build | The `WITH_TESTS=OFF` product build passed and contains exactly 17 plugin dylibs |
| Current Simplified Chinese | All EtherCAT translation contexts contain 0 unfinished and 0 empty translations |
| Current hardware evidence | The generic Start lifecycle and explicit DC lifecycle each passed 3 tests with 0 failures and completed safe cleanup in `SHUTDOWN`/EMPTY with no lease owner or fault |
| Current FreeRun boundary | Windows `ISSUE-RT-009` separated a successful LRW cycle from the actual failure: `OP_REQUEST` for station `0x1002` returned WKC 0/1 on all four attempts. `cfg812` was not activated and no FreeRun lifecycle ran. Windows continues with `ISSUE-API-016` and the smallest isolated fix |
| Current controller safety | The failed attempt was safely rolled back to `B/12/813`, `OP_SAFE`, WKC 11/11, faults 0, and lease 0 |
| Current UI verification | The earlier single-instance product observation showed Workbench, Simplified Chinese, hidden production Mock UI, and the compact tree. The current endpoint/output round passed 95 widget-level Workbench tests plus a 3-pass passive Application Output lifecycle without connecting hardware |
| Historical hardware evidence | On 2026-07-24 RAM-only v1.10 passed Connect, Acquire, Configuration, three-slave Scan, Release, and safe cleanup; Restore returned typed `CAPABILITY_MISMATCH (-20)` |
| Historical runtime observation | The 2026-07-24 RAM service negotiated v1.10/`0xfff`; the recorded reboot behavior returned to persistent release24/v1.9 |
| Publication | Current work remains local on `embed-labs`; no remote publication is authorized |

## Headless ECPKG deployment session qualification

This is the current offline qualification for the provider-neutral deployment
session. It does not authorize a real-controller package mutation.

| Gate | Result |
|---|---|
| Windows authority | Latest audited authority was commit `6af2f4878f5d40c47a7cd2ffff5ace932efc0c2b`; `product_api_v1.md` SHA-256 was `dc3fa69c97b3e36ff3ce51aef7e0610e9e79f680953412a8b7af0337d411afbb` |
| Provider boundary | Core exposes an optional immutable package transaction and cancellation request; default Providers reject both, and no vendor message number crosses the interface |
| Exact sequence | Begin, every Chunk, Commit, and Abort each require one terminal BulkStatus; Validate, Activate, and Rollback require successful stages 1 through 4 with `final=0`, followed by PackageState |
| Idempotency | OperationId is a client-only bounded journal key; exact replay returns the stored semantic result, conflicting reuse is rejected, and a historical replay cannot replace a different active deployment; it is not represented as a Product API wire field |
| Ambiguous result | Timeout, disconnect, or incoherent terminal PackageState becomes OutcomeUnknown; the mutation is not replayed |
| Rollback safety | An explicit Activate failure first queries authoritative PackageState and rolls back only an exact confirmed candidate/fallback pair; an ambiguous final result never triggers speculative rollback |
| Cancellation | Cancellation is limited to upload/commit and becomes Canceled only after a successful BulkAbort response |
| Input bound | Empty packages and ECPKG artifacts larger than the protocol maximum of 16 MiB are rejected before BulkBegin |
| Application Output | Workbench reports deployment state, OperationId, byte progress, and detail through the unified Application Output stream; Failed and OutcomeUnknown use error severity |
| Core regression | 20 passed, 0 failed |
| ProductApi regression | 77 passed, 0 failed, 1 real-hardware lifecycle skipped by its explicit environment gate |
| Workbench focus | Deployment-output presentation and existing controller-output workflow each passed in isolated offscreen runs; each run recorded initialization, the selected test, and cleanup |
| Workbench broad boundary | The complete suite is not claimed: the pre-existing navigation command test still times out locating the unsupported device, and an early failure leaves a later tree-model fixture unsafe |
| Translation | The 407 strings extracted from the changed Core, ProductApi, and Workbench sources have 0 missing and 0 unfinished Simplified Chinese entries; XML validation and `lrelease` passed |
| Product build | Qt 6.11 `WITH_TESTS=OFF` EtherCATCore, EtherCATProductApi, and EtherCATWorkbench targets passed |
| Hardware claim | None; this issue opened no controller socket and sent no lease, Bulk, package, state-transition, or runtime request |
| Publication | Local `embed-labs` only; no fetch, pull, merge, rebase, branch switch, push, PR, or remote publication |
