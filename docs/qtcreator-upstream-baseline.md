# Qt Creator Local Product Baseline

## Authority

The authoritative baseline for the EtherCAT configurator work is the current
local checkout, not a remote branch:

- Repository: `/Users/kvell/kk-project/qt-project/qt-creator`
- Required branch: `embed-labs`
- Baseline commit: `a7a6f4ebdc2d07a8812d717b1c4fc165d1a8df71`
- Baseline description: `v20.0.0-87-ga7a6f4ebdc2`
- Product version: `20.0.1`
- Baseline date: 2026-07-16

All implementation and documentation changes must stay on `embed-labs`.
Completed major features may be committed locally, but they must not be pushed
or published without explicit user approval.

## Remote update lock

Remote code import is disabled by policy. Do not run `git fetch`, `git pull`,
`git merge`, or `git rebase` unless the user explicitly asks for a remote
update. Existing remote-tracking references are informational snapshots only.

An `upstream` remote was configured and fetched once during the initial audit,
before this lock was requested. That fetch did not change the active branch,
worktree, or local commit history. The references must not be used to update
the product baseline without new user authorization.

## Source baseline

The current local product history includes:

- A merge of Qt Creator 20.0 commit
  `11ba5cec09dce75db4bc948d98055e338ff59576`.
- Existing Embed Labs branding and macOS application assets.
- The existing `EasyBoard` plugin and its Core extension point.
- Historical product cleanup, including removal of older changelog files.

There is no EtherCAT plugin or EtherCAT source directory in the baseline.

## Local customization summary

Compared with the recorded Qt Creator 20.0 merge parent, the local branch has:

- 35 commits after the recorded `origin/20.0` reference.
- 193 changed paths.
- 4,736 added text lines and 16,192 deleted text lines.
- 37 files under `src/plugins/easyboard`.
- 28 changed or added paths under `src/app`.
- 5 changed or added paths under `src/plugins/coreplugin`.
- 118 changed paths under `dist`, primarily historical changelog removal.

These figures are audit evidence, not a request to rewrite or remove the
existing product work.

## Toolchain snapshot

The local machine audit found:

| Component | Observed value |
|---|---|
| Host | macOS 26.3.1, arm64 |
| Xcode | 26.1 |
| Apple Clang | 17.0.0 |
| CMake | 4.2.3 |
| Ninja | 1.12.1 |
| Python | 3.14.4 |
| Older Qt installation | Qt 6.8.3 at `/Users/kvell/Qt/6.8.3/macos` |
| Qualified product Qt | Qt 6.11.0 at `/opt/homebrew` |

Qt 6.11.0 is the qualified local product toolchain. A clean Qt 6.8.3 build was
attempted with Homebrew excluded from CMake discovery, but the link failed at
`Nanotrace` because that Qt package requests the `AGL` framework removed from
the current Xcode 26 SDK. This is a host/toolchain incompatibility, not an
EtherCAT source defect. Do not patch product source to emulate the removed
framework. The minimum supported Qt version remains a separate release-policy
decision.

## Existing build evidence

The audit found several adjacent build directories. The executable in
`../qt-creator-build-easy-board-20-homebrew/Embed Labs.app` starts in command
line mode and reports:

- Embed Labs 20.0.1
- Qt 6.11.0
- Core, TextEditor, ProjectExplorer, CppEditor, Debugger, QtSupport,
  ResourceEditor, RemoteLinux, and EasyBoard plugins

This proves that a previous local product build artifact can load those nine
plugins. It is historical evidence only; the current-source evidence is
recorded below.

## Current-source build evidence

The current baseline was configured and built from a fresh directory at
`../qt-creator-build-ethercat-stage0-qt611` with Qt 6.11.0, Release mode,
QML Designer disabled, and this explicit plugin allow-list:

```text
Core;TextEditor;ProjectExplorer;CppEditor;Debugger;QtSupport;
ResourceEditor;RemoteLinux;QmakeProjectManager;EasyBoard
```

The build completed all 1,998 Ninja actions. The built executable reports
Embed Labs 20.0.1, Qt 6.11.0, and exactly those ten plugins. A clean-settings
GUI smoke remained running for 15 seconds with no crash or plugin-load output,
then was intentionally terminated with `SIGTERM`; exit status 143 is therefore
the expected test-harness termination result.

`QmakeProjectManager` is part of the allow-list because the existing Debugger
plugin metadata requires it. It is a baseline dependency, not an EtherCAT
feature.

After stage 1, the normal product profile adds `EtherCATCore` as the eleventh
plugin and `EtherCATData` as its product-owned library. The same Qt 6.11.0
Release build completes successfully. Stage-0 evidence above remains the
pre-EtherCAT reference point.

Stage 2 adds the product-owned `EtherCATProject` plugin through public
ProjectExplorer, document, MIME, and wizard APIs. The qualified Release
product profile now contains 12 plugins and still has no EtherCAT-specific
change under Qt Creator Core, ProjectExplorer, or the application bootstrap.

Stage 3 adds the product-owned `EtherCATDevices` plugin, bringing the qualified
profile to 13 plugins. Stage 4 adds `EtherCATWorkbench`, bringing it to 14
plugins. Both use product-owned public contracts and standard Qt Creator plugin
registration; the direct upstream Core patch count remains unchanged. The
stage-4 normal product build, `-version` plugin inventory, and enabled/disabled
startup evidence are recorded in `docs/compatibility-matrix.md`.

## Worktree preservation

`AGENTS.md` was untracked when the audit began. It is an authoritative user
instruction file and must remain unmodified and uncommitted unless the user
explicitly requests otherwise. All stage-0 commits must stage named project
files only.

## Baseline verification status

| Requirement | Status | Evidence |
|---|---|---|
| Correct branch | Verified | `git status --short --branch` reported `embed-labs` |
| Local baseline commit | Verified | `git rev-parse HEAD` |
| Product version | Verified | branding CMake file and existing executable `-version` |
| Existing EasyBoard plugin | Verified | source, CMake, qbs, metadata, and executable plugin list |
| EtherCAT plugins absent | Verified | repository filename and directory search |
| Clean current-source product build | Verified | Qt 6.11.0 Release build completed 1,998/1,998 actions |
| Product plugin allow-list | Verified | Executable and bundle contain exactly ten listed plugins |
| Clean-settings GUI startup | Verified | Stable for 15 seconds, then intentionally sent `SIGTERM` |
| Qt 6.8.3 build on current host | Blocked | Xcode 26 SDK has no `AGL` framework requested by Qt 6.8.3 |
| Upstream merge readiness | Deferred | Remote updates forbidden without explicit request |
