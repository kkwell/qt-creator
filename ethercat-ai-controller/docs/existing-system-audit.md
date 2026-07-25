# Existing System Audit

## Audit identity and evidence boundary

`ISSUE-AI-CONTROLLER-001` was qualified against the current local Qt Creator
checkout:

- branch: `embed-labs`;
- baseline commit:
  `e9f64673da80545b6927f28fc9ba26a18caa88da`;
- baseline subject: `EtherCAT: Complete controller control workflow`;
- remote import: none; and
- pre-existing worktree residue: untracked root `AGENTS.md`, not modified or
  staged by this issue.

The Windows master source named in the product goal,
`C:\Users\Kvell\Project\zynq\ethercat-master`, is not mounted in this
workspace. Therefore this is a source audit of the current local Qt Creator
adapter and its checked-in controller records, not a fresh WSL audit of that
Windows repository. Any future production bridge must repeat the Product API,
ECPKG, ETIR, ABI, toolchain, and hardware audit inside WSL.

## Product API

### Present in the current checkout

The existing `EtherCATProductApi` plugin owns:

- Product API v1.10 with bounded v1.9 compatibility;
- three ECAP channels, fixed framing, CRC32C, payload bounds, sequence,
  SessionId, BootId, RequestId, reconnect, and event-resume correlation;
- generic immutable connection, state, capability, package, firmware,
  topology, control-progress, and error summaries;
- exclusive control-lease acquire/heartbeat/release;
- typed configuration, topology discovery, active-package restore, Start,
  explicit FreeRun/DC Start, Pause, Resume, and ControlledStop; and
- closed message allow-lists that reject arbitrary numeric Product API
  commands before frame creation.

Primary evidence:

- `src/libs/ethercatdata/controllerconnection.h`;
- `src/plugins/ethercatproductapi/productapicodec.*`;
- `src/plugins/ethercatproductapi/productapisession.*`;
- `src/plugins/ethercatproductapi/productapiconnectionprovider.*`; and
- `docs/ethercat-product-api.md`.

### Not present as a reusable AI Gateway protocol

The existing provider is an in-process Qt Creator contract. It does not expose
MCP, REST, OpenAPI, JSON Schema, OAuth identity, a durable AI audit log, or
cross-client idempotency storage. Its current control path is a product UI
workflow, not a vendor-independent controller-tools API.

This issue deliberately does not call that provider. It defines a mock-only
boundary so a later bridge cannot accidentally widen the existing closed
Product API allow-list.

## ECPKG

The checkout models staged and active package summaries and can restore the
exact already-persistent active package. It also classifies validated active
ECFG/DC timing metadata for explicit Start mode checks.

It does not:

- construct an ECPKG;
- compile or insert ECFG/ETIR content;
- upload, stage, accept, sign, activate, or atomically switch a package;
- create A/B deployment transactions from an offline project; or
- provide an ECPKG schema/SDK to an AI client.

The new `ControllerProject` format therefore carries nullable ETIR/ECPKG
content hashes and a build state. The checked-in mock project is `not-built`,
has both hashes `null`, selects no slot, and has no signature.

## ETIR

No ETIR compiler, bytecode schema, interpreter, WCET analyzer, or CPU1 task
executor is implemented in this checkout. `ControllerCapabilitySummary` can
report a `runtimeProgram` capability, but a Boolean capability field is not an
ETIR implementation.

The existing `*.ecatproject` version 2 format explicitly documents that it is
local editor data and not ETIR, ECFG, ECPKG, or a controller network message.
This issue defines the `ecl-v1` program graph boundary only. It never emits or
executes ETIR.

## ESI

### Present

`EtherCATDevices` and `EtherCATData` already provide a useful phase-one ESI
base:

- content-addressed original XML storage with SHA-256;
- Vendor ID, Product Code, Revision, localized name, type, and group;
- SyncManager address, size, control, enable, and direction;
- RxPDO/TxPDO entries, mapping flags, widths, data types, and assignment;
- CoE capability flags;
- startup SDO transition, address, raw bytes, and comment;
- literal DC mode/AssignActivate/Sync0/Sync1 values; and
- immutable device descriptions plus offline PDO, startup, DC, and process
  image validation.

Primary evidence:

- `src/libs/ethercatdata/devicedescription.h`;
- `src/libs/ethercatdata/offlineconfiguration.*`;
- `src/plugins/ethercatdevices/esiparser.*`;
- `src/plugins/ethercatdevices/devicerepository.*`; and
- `docs/ethercat-devices-repository.md`.

### Missing for a universal device model

The current phase-one model does not provide:

- mailbox offsets/sizes and complete protocol detail in the public model;
- FMMU descriptions;
- a complete CoE object dictionary;
- standard semantic names, scaling, engineering units, and access contracts;
- CiA402 axis/mode/function-block capability normalization;
- modular `Modules`/`Slots` composition;
- DC formula evaluation and device cycle limits;
- watchdog and safe-output semantics;
- versioned vendor-private initialization and recovery rules; or
- signed, evidence-backed adapter qualification.

The `NormalizedDeviceModel` schema fills the data boundary for these fields.
The three mock models do not change or extend the production ESI parser.

## Device adapter implementations

The current `EtherCATProductApi` plugin is a controller transport adapter, not
a per-slave device adapter registry. The checkout has no versioned
`AdapterManifest`, identity range, ESI-hash allow-list, validation state,
evidence bundle, or signature rule for device behavior.

This issue adds a contract and three `mock-only` manifests. None is signed;
all set `realHardwareAllowed` to `false`. A production `verified` manifest is
rejected unless it has a signature and passed schema-validation plus
engineering-review evidence.

## Existing versus new protocol

| Capability | Existing checkout | Added by this issue | Still requires a new protocol/implementation |
|---|---|---|---|
| Product API transport | v1.10/v1.9 adapter | Audited only | Gateway bridge and production auth |
| Controller state | Immutable provider snapshot | Mock REST/MCP projection | Live provider projection and durable audit |
| Topology | Typed linear result after leased discovery | Static mock topology read | Read-only cached topology API; physical edges remain absent |
| ESI | Phase-one parser/repository | NDM contract and fixtures | Full dictionary/FMMU/CiA402/modular normalization |
| Device adapter | No per-device registry | Manifest contract and mock registry | Signed production registry and SDK |
| Offline project | `*.ecatproject` v2 | ControllerProject v1 contract | Deterministic transformation and traceability |
| ETIR | Not implemented | Hash/reference boundary only | Language, compiler, validator, WCET, CPU1 VM |
| ECPKG | State/restore existing package | Hash/reference boundary only | Builder, signer, uploader, stage/activate/rollback |
| MCP/OpenAPI | Not implemented | Read-only mock skeleton | OAuth, production discovery, policy, persistent audit |

## Hardware evidence

No hardware command, Product API socket, EtherCAT scan, lease, configuration,
SDO/PDO access, deployment, task start, or motion operation was performed for
this issue. Historical hardware observations in existing documentation were
read as source evidence only and were not refreshed.
