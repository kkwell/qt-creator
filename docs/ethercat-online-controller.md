# EtherCAT Online Controller Integration

## Scope

This document is the engineering handoff for the staged online EtherCAT
integration. It separates five evidence classes:

- offline project configuration;
- local Mock scan and diagnostics;
- read-only real-controller communication;
- stateful discovery under a control lease; and
- controlled writes such as package activation, SDO/PDO output, run control,
  or firmware changes.

`ISSUE-CORE-CONTROLLER-CONNECTION-API-001` adds only the in-process semantic
contract required by a future controller plugin. It adds no socket, protocol
codec, UI, bus scan, state transition, or hardware execution.

## Controller endpoints

| Channel | Endpoint | Intended use |
|---|---|---|
| Control | `192.168.3.101:15200` | Handshake, state, commands, and read-only queries |
| Push | `192.168.3.101:15201` | State, alarm, performance, firmware, and heartbeat push |
| Bulk | `192.168.3.101:15202` | Capability and bulk transfer |
| Base endpoint | `192.168.3.101:15200` | User-facing Qt/Python connection value |

The future `EtherCATProductApi` plugin owns these defaults and all
`Qt::Network` use. `EtherCATData`, `EtherCATCore`, Project, Devices,
Workbench, Scan, and Diagnostics do not own sockets or ECAP frames.

## Authoritative protocol sources

The authoritative files are read-only on
`C:\Users\Kvell\Project\zynq\ethercat-master`:

- `docs\knowledge_base\protocols\product_api_v1.md`
- `docs\knowledge_base\diagnostics\diagnostic_catalog_v1.md`
- `tools\tests\product_firmware_hardware_gate.py`
- `igh_osless\tools\product_api_client.py`

The 2026-07-23 audit copies have these SHA-256 values:

| File | SHA-256 |
|---|---|
| `product_api_v1.md` | `95bc5ded13b7403322d959b45048564c8bf0f3ae527785a98e73ef3b9d0079ea` |
| `diagnostic_catalog_v1.md` | `46415f76f1f5153a5c523cecfd161fa7828cdd01f33d7b6029f2f5ec62c2e4fc` |
| `product_api_client.py` | `0a6da19e764fb1719661771bf28f91c6080088b348fc3252ecd7b7954eb1ccdd` |
| `product_firmware_hardware_gate.py` | `d8fb11c050dde6fcefa77c0bb5d47243911d7a5e602896e6526edcc64eaee0e6` |

A changed hash requires a new protocol audit before implementation assumptions
are reused. The Windows repository is not modified by this Qt product.

## Product API safety boundary

Product API v1.9 uses a fixed ECAP frame contract and one SessionId/BootId
across Control, Push, and Bulk. The future implementation must validate channel
role, negotiated version, payload limit, SessionId, BootId, RequestId,
Sequence, message length, and CRC before publishing a semantic snapshot.

These operations are read-only and require no control lease:

- handshake;
- `GetState`;
- `GetCapability`;
- `GetPackageState`;
- `GetTimeCorrelation`;
- `GetFirmwareState`; and
- event subscription/recovery.

`DiscoverTopology` and `DiscoverModules` are not ordinary read-only refreshes.
They require:

1. a current control lease;
2. explicit entry into configuration mode;
3. confirmed `SHUTDOWN`;
4. the discovery command and its staged completion;
5. exit/cleanup and lease release; and
6. before/after state, BootId, package, fault, and lease-owner comparison.

The current protocol contract additionally requires no active package for
discovery. Connecting, opening a page, refreshing, or starting the product must
never trigger this workflow implicitly.

## Read-only real-controller evidence

On 2026-07-23 the authoritative Windows Python client was used against the
three controller endpoints. No lease was acquired, no state was changed, and
no discovery or write was requested.

Observed:

- Product API v1.9 handshake succeeded.
- Control and Bulk reported the same BootId.
- Feature bits were `0x7ff`.
- The default control lease was 5,000 ms and the lease owner was zero.
- Service state was `SHUTDOWN`; status contained `READY`.
- Aggregate severity, current faults, and latched faults were zero.
- Expected/actual WKC and cycle count were zero.
- Capability reported 64 slaves, 125,000 ns minimum cycle, eight cyclic
  frames, and 4,096-byte process input/output limits.
- Firmware state was `CONFIRMED`, active slot A, with no failure.
- A persistent active configuration package exists.

This proves only the reference-client read-only path. It is not evidence that
the Qt product already connects to hardware.

## Confirmed issues and handoff

### Client defect: firmware push before event-resume result

The reference Python client's `resume_events(0)` can fail with:

`ProductApiError: unexpected frame before ResumeEventsResult`

A manual read showed legal RequestId-zero replaceable frames before the result:
ControllerState, PerformanceSnapshot, FirmwareProgress, and PushHeartbeat.
Product API v1.9 explicitly permits the firmware progress snapshot on a new
subscription. `_consume_replaceable_push()` handles the other replaceable
frames but omits FirmwareProgress.

Recommended controller-client repository change:

1. accept `MSG_FIRMWARE_PROGRESS` in `_consume_replaceable_push()`;
2. validate it through `decode_firmware_state(frame)`;
3. return `True`; and
4. add a regression where FirmwareProgress precedes ResumeEventsResult.

This is a confirmed reference-client defect, not evidence of a server defect.

### Master API gap: no physical topology edges

The current topology result exposes position, station address, AL state,
VendorId, ProductCode, RevisionNo, and SerialNo. Link diagnostics provide
per-port counters, but neither response identifies parent/child ports, peer
nodes, or physical edges.

The Qt product can therefore render only a truthful linear scan order.
Authoritative branch, star, or port-to-port topology requires a versioned
master API containing at least:

- node identity;
- local port;
- peer node identity;
- peer port; and
- link/edge validity or generation.

Until that ABI exists, the UI must not infer a graph from station position.

### Master security gap

Product API v1.9 does not provide authentication, authorization, or transport
encryption. It is suitable only on an isolated management network. Broader
deployment requires a separately versioned security design and cannot be
claimed as a Qt client-only fix.

### Stateful discovery risk, not yet a defect

The current controller has an active package while the discovery contract
requires no active package and a leased transition to `SHUTDOWN`. This is a
safety precondition conflict that must be resolved and explicitly authorized
before a real scan. It is not classified as a master defect without a failed
request and controller-returned diagnostic evidence.

### Qt product gaps

The current Qt product still lacks:

- the concrete `EtherCATProductApi` transport plugin;
- an embedded Communication page and Connect/Disconnect actions;
- Provider-neutral real/Mock scan routing;
- the leased discovery state machine;
- a separate Current Bus (Actual) tree;
- ESI re-match and config/actual Apply;
- an embedded Project/Actual/Overlay topology page; and
- real Push/Bulk diagnostics mapping.

## CODESYS-informed workflow

The product follows the useful CODESYS separation without copying its visual
assets or window layout:

- install XML through a software-managed
  [Device Repository](https://content.helpme-codesys.com/en/CODESYS%20Development%20System/_cds_cmd_device_repository.html);
- run an explicit
  [Scan for Devices](https://content.helpme-codesys.com/en/CODESYS%20EtherCAT/_ecat_cmd_scan_devices.html)
  on the selected master;
- retain the actual result separately and compare it with the project before
  applying changes, as in the official
  [EtherCAT commissioning flow](https://content.helpme-codesys.com/en/CODESYS%20EtherCAT/_ecat_tutorial.html);
- preserve an unknown actual device when its ESI is absent, then import and
  re-match instead of guessing; and
- keep the left device tree responsible for selection while the right side
  hosts embedded editors and topology.

The existing software-managed ESI directory is
`Core::ICore::userResourcePath()/ethercat/esi`. Imported XML is validated,
copied to content-addressed storage, and indexed; users should not mutate that
internal directory directly.

## Delivery order

1. `ISSUE-CORE-CONTROLLER-CONNECTION-API-001`
2. `ISSUE-ONLINE-CONNECTION-PAGE-001`
3. Provider-neutral Scan workflow
4. Product API discovery state machine
5. ESI match/import/re-match
6. Project Configuration versus Current Bus tree and Apply
7. Embedded Project/Actual/Overlay topology
8. Real diagnostics Provider

Each issue is independently tested and committed locally on `embed-labs`.
No Qt product commit is pushed to a remote repository.
