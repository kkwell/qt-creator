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

### Planned EtherCAT additions

Plugins enter the profile only after their preceding serial gate passes:

1. `EtherCATDevicesPlugin`
2. `EtherCATWorkbenchPlugin`
3. `EtherCATScanPlugin`
4. `EtherCATDiagnosticsPlugin`

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
| TwinCAT-inspired device tree | Planned in Workbench plugin |
| Offline EtherCAT project | Stage 2 verified |
| ESI repository | Planned in Devices plugin |
| Scan UI and topology comparison | Planned with Mock provider only |
| WKC/DC/link diagnostics | Planned with Mock provider only |
| Zynq protocol | Explicitly out of scope |
| Real EtherCAT scan | Explicitly out of scope |
| ECPKG/ECFG/ETIR | Explicitly out of scope |
| ST/LD/FBD | Explicitly out of scope |

## EtherCATCore stage-1 qualification

| Check | Result |
|---|---|
| Focused Qt Creator plugin tests | 9 passed, 0 failed |
| Normal Release product build | Passed with 11-plugin allow-list |
| Enabled clean-settings startup | Passed; stable until intentional `SIGTERM` |
| Explicitly disabled startup | Passed with `-noload EtherCATCore` |
| Normal plugin shutdown | Passed through automatic plugin-test exit |
| Repeated process startup | Passed across focused test and two smoke runs |
| Direct upstream Core or app changes | None |
| Full product build with `WITH_TESTS=ON` | Blocked by existing EasyBoard test include defect |

## EtherCATProject stage-2 qualification

The public Project contract uses immutable `EtherCATData` snapshots and
`ProjectExplorer::ProjectManager` lifecycle signals. ProjectExplorer objects,
documents, models, and indexes never cross the plugin boundary.

| Check | Result |
|---|---|
| Focused Qt Creator plugin tests | 7 passed, 0 failed |
| Format round trip and corruption | Passed |
| Version-0 migration and exact backup | Passed |
| Undo/Redo and Save All modified state | Passed |
| Atomic save failure preserves source | Passed |
| Two-project open/switch/close lifecycle | Passed |
| Normal Release product build | Passed with 12-plugin allow-list |
| Enabled and disabled GUI startup | Passed; stable until intentional interrupt |
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
