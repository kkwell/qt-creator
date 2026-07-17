# Local Compatibility Matrix

## Baseline matrix

| Item | Supported or observed baseline | Evidence status |
|---|---|---|
| Product branch | `embed-labs` only | Verified |
| Product commit | `a7a6f4ebdc2d07a8812d717b1c4fc165d1a8df71` | Verified |
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
| EtherCATWorkbench | EtherCAT mode, device tree, selection, and offline property pages | Stage 4 verified |
| EtherCATScan | Local Mock scan, topology comparison, and checked acceptance | Stage 5 verified |
| EtherCATDiagnostics | Local Mock state, WKC, DC, alarm, and performance views | Stage 6 verified |

### Phase-1 EtherCAT profile

All six planned EtherCAT feature and infrastructure plugins have entered the
local profile. This proves the plugin profile is assembled, not that every
Phase-1 requirement is complete. Editable Process Data, CoE Online Mock,
Startup, and DC are now verified, and the public derived-node kinds are
reserved. The visible
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
| Offline Process Data/Startup/DC domain model | Verified in EtherCATData |
| Project persistence and Undo/Redo for those models | Verified in format version 2 |
| Editable Process Data page | Verified in current Workbench issue |
| CoE Online Mock and explicit Add to Startup | Verified in current Workbench issue; no SDO or controller access |
| Editable Startup page | Verified in current Workbench issue |
| Editable DC page | Verified in current Workbench issue |
| Offline EtherCAT project | Stage 2 verified |
| ESI repository | Stage 3 verified |
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

## EtherCATProject current qualification

The public Project contract uses immutable `EtherCATData` snapshots and
`ProjectExplorer::ProjectManager` lifecycle signals. ProjectExplorer objects,
documents, models, and indexes never cross the plugin boundary.

| Check | Result |
|---|---|
| Focused Qt Creator plugin tests | 11 passed, 0 failed |
| Format round trip and corruption | Passed |
| Version-0 and version-1 migration with exact backups | Passed |
| Version-2 Process Data, Startup, and DC persistence | Passed |
| Missing configuration, duplicate stable ID, invalid raw hex, and invalid mapping rejection | Passed |
| Undo/Redo and Save All modified state | Passed |
| Atomic save failure preserves source | Passed |
| Two-project open/switch/close lifecycle | Passed |
| Offline slave validation, persistence, service command, and Undo/Redo | Passed |
| Process Data, Startup, and DC service commands and Undo/Redo | Passed |
| Scan acceptance preserves matched offline configuration | Passed |
| EtherCAT plugin regressions | Core 16, Project 11, Devices 8, Workbench 9, Scan 7, Diagnostics 7 passed |
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
| Focused EtherCATWorkbench plugin tests | 20 passed, 0 failed |
| Six-plugin EtherCAT regression | 70 passed, 0 failed in isolated processes |
| Failure-first tree contract test | Failed to compile on missing source-ID routing before implementation, as expected |
| Failure-first navigation layout test | Failed on `ElideRight`, then on missing accessible metadata, before both fixes |
| Failure-first CoE Online page test | Compiled and failed on the missing `CoE Online` page descriptor before implementation, as expected |
| Failure-first unified-status test | Compiled and failed because no Workbench status-bar control was registered, as expected |
| Failure-first command-strip test | Compiled and failed because EtherCAT Mode had no engineering command strip, as expected |
| Failure-first provider-state tree test | Failed to compile on missing difference/issue/diagnostics navigation APIs before implementation, as expected |
| Failure-first context-command test | Failed to compile only on missing Locate Unsupported/Copy Node ID command IDs and Controller requests before implementation, as expected |
| Failure-first offline-topology test | Failed to compile only on the four missing Add/Remove/Move ActionManager command IDs before implementation, as expected |
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
| Supported ESI device add and repeated-device unique naming | Passed with complete identity, repository reference, Process Data, Startup, and DC defaults |
| Offline slave remove/reorder workflow | Passed with normalized positions, boundary enablement, stable selection, selection repair, Undo, and Redo |
| Offline-topology ActionManager identity | Passed for all four context-only commands in real device and configured-slave popup menus |
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
| macOS accessibility lock-transition observation | One Qt 6.11 accessibility crash was captured during a lock transition; a second RxPDO-selection run remained alive until the Mac locked, so the event is not reproduced and remains a qualification risk |
| Dynamic property-page provider removal | Passed |
| Dynamic Scan/Diagnostics availability and removal | Passed |
| Scan snapshot overlay | Passed for exact, Missing, Added, Revision, Vendor, source label, full-detail search, and aggregate count/severity |
| Diagnostics snapshot overlay | Passed for Run/OP, SAFEOP/error, AL detail, missing snapshot, alarm/error marker, stopped state, and cleanup |
| Difference/issue/Diagnostics ActionManager navigation | Passed with stable selection, filter clearing, ancestor expansion, and shared QAction registration |
| Tree context and navigation ActionManager identity | Passed for seven navigation and four offline-topology commands, shared Expand/Collapse buttons, context-only strip exclusion, enabled state, stable-ID copy, and placeholder protection |
| EtherCATCore regression tests | 17 passed, 0 failed |
| EtherCATProject regression tests | 11 passed, 0 failed |
| EtherCATDevices regression tests | 8 passed, 0 failed |
| EtherCATScan regression tests | 7 passed, 0 failed |
| EtherCATDiagnostics regression tests | 7 passed, 0 failed |
| Product version inventory | All 16 allow-listed plugins present and recognized |
| Normal Release product build | Passed with 16-plugin allow-list |
| Enabled GUI startup | Passed with clean temporary settings and all 16 plugins for 5 seconds after delayed initialization; intentionally interrupted after profile output |
| Explicitly disabled startup | Passed with `-noload EtherCATWorkbench` and clean temporary settings for 5 seconds after delayed initialization; intentionally interrupted after profile output |
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
| Direct upstream Core, ProjectExplorer, or app changes | None |
| Full product build with `WITH_TESTS=ON` | Blocked by existing EasyBoard test include defect |
| qbs build | Not run; qbs executable is unavailable |

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
