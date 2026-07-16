# Local EtherCAT Diagnostics

## Scope

`EtherCATDiagnostics` is the phase-1, local-only diagnostics feature plugin.
It presents TwinCAT-inspired online-state, WKC, DC, link, error, alarm, and
cycle views while consuming synthetic data from a Mock Provider.

The plugin does not open a network socket, access an EtherCAT interface,
connect to a controller, parse a controller protocol, or claim a hardware
measurement. Every user-visible page, action, status, event, and AL Status
value identifies itself as Mock data.

## Ownership and dependencies

The plugin depends on public product contracts only:

- `EtherCATData` supplies immutable diagnostic values.
- `EtherCATCore` supplies `DiagnosticsProvider`, `ProjectService`,
  `SelectionService`, `StateService`, and the property-page contract.
- `EtherCATProject` supplies the open offline project and configured slaves.
- `EtherCATWorkbench` supplies public menu, context, and node-kind IDs.
- Qt Creator Core supplies `ActionManager` and `MessageManager`.

The plugin does not include a Workbench `Internal` header, locate another
plugin's widget, or modify Qt Creator Core, ProjectExplorer, or application
code. Its Provider and property-page Provider are registered in the Qt Creator
object pool and removed before their owned objects are destroyed.

## User workflow

The Workbench shows a derived `Diagnostics` child for each displayed master
when a diagnostics Provider is available. Selecting that child contributes
these pages:

| Page | Information |
|---|---|
| Master Overview | Run mode, ESM, AL Status, WKC, slave and alarm totals |
| Slave States | Per-slave ESM, WcState, AL Status, link, and DC state |
| WKC / WcState | Expected and actual WKC plus mismatch counts |
| DC Sync | State, offset, deviation, maximum offset, and lost-sync count |
| Link / Ports | Link, communication, interruption, and receive counters |
| Frame / Errors | Lost, CRC, timeout, drop, late, and overflow counters |
| Events / Alarms | Severity, lifecycle, code, node, summary, and repeat count |
| Cycle / Performance | Cycle, jitter, deadline, history drops, and trend |

Master and configured-slave selections also receive one `Mock Online` page.
The phase-1 project format contains one master, so a derived Diagnostics node
resolves to the first master of its project. A multi-master project revision
must add an explicit master identity to the Workbench context before this rule
can be widened.

The EtherCAT menu and every diagnostics page expose these context-sensitive
commands:

- Start and Stop Mock Diagnostics.
- Mock Config, Mock FreeRun, and Mock Run / OP.
- Acknowledge the selected or first active Mock alarm.
- Clear recovered Mock events.

The real registered `QAction` owns its enable and checked state. Qt Creator's
`Command` proxy is used only for menu and tool-button presentation, preserving
the official ActionManager context-routing model.

## Sampling and publication

The `MockDiagnosticsProvider` stays on the GUI thread. It owns one sampler
object moved to a dedicated `QThread`. The sampler uses a precise timer and
produces lightweight numeric records; it never reads project objects or
touches a widget.

The default source period is 10 ms and the default GUI publication period is
100 ms. Source samples are batched in the worker thread. One queued batch is
aggregated into one immutable snapshot on the GUI thread, so repaint frequency
does not follow source frequency. Snapshots expose source and coalesced counts
to make the rate limiting observable.

Event and trend histories are bounded independently. Their default capacities
come from the EtherCAT Core maximum-recent-events setting. When a capacity is
reached, the oldest record is removed and the matching dropped counter is
incremented. Test-only Provider limits can shorten periods and capacities
without changing the production defaults.

## Mock scenarios

| Scenario | Synthetic effect |
|---|---|
| Normal Mock data | Valid WKC, links up, synchronized DC, positive margin |
| Mock WKC mismatch | Actual WKC below expected and timeout growth |
| Mock link interruption | Port down, lost frames, and periodic CRC growth |
| Mock DC drift | Offset beyond the Mock sync threshold and late frames |
| Mock deadline pressure | Negative deadline margin, drops, and overflows |
| Mock alarm burst | Repeated unique alarms for bounded-history testing |
| Mock source failure | Worker failure after three published batches |

The synthetic expected WKC is two per configured offline slave. This is a Mock
test rule, not a general EtherCAT WKC calculation or a controller contract.

## State, alarms, and cleanup

A normal session follows `Stopped -> Starting -> Running -> Stopping ->
Stopped`. Initial data is explicitly marked Mock and begins at INIT; the first
published batch enters the selected Config/PREOP, FreeRun/SAFEOP, or Run/OP
mode. A source failure stops the sampler and enters `Failed`. An explicit stop
then clears the active request and returns to `Stopped`.

Alarm conditions create one Active event per code and node. Repeated samples
increment its repeat count. Acknowledgement changes Active to Acknowledged;
condition recovery changes either state to Recovered. Clearing affects only
Recovered entries.

Cleanup is deterministic:

- Closing the monitored project stops the session.
- Changing its offline topology stops the session, preventing stale WKC use.
- Closing the final page consumer stops a running session.
- Source failure stops and joins the worker thread.
- Plugin shutdown removes pages, stops the workflow and sampler, unregisters
  Providers, and completes synchronously.

## UI rules

Pages use Qt Creator palette colors, `StyleHelper` fonts, and spacing tokens.
They contain no hard-coded colors, fonts, or fixed pixel dimensions. The trend
is painted from bounded Provider values. The Mock banner, current context,
scenario, stream state, source period, and publish period remain visible on
every page.

## Current limits

- There is no real-controller diagnostics Provider.
- Values are not persisted after a session or application exit.
- Only one `{projectId, masterId}` session can run at a time.
- There is no event export, alarm journal, protocol trace, or packet capture.
- Run-mode buttons change only the local simulation.
- Network protocol and Zynq integration remain separate future work.

## Qualification

Stage 6 is qualified by focused Provider, action, page, lifecycle, failure,
bounded-history, and live-update tests; the preceding five EtherCAT suites;
the 16-plugin Qt 6.11 Release product build; enabled and disabled startup
smokes; and direct desktop inspection with a two-slave offline project.

The complete `WITH_TESTS` default target remains blocked by the pre-existing
EasyBoard include of unavailable `extensionmanager_test.h`. The qbs project is
kept synchronized, but no qbs executable is installed on this host.
