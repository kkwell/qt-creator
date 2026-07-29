# EtherCAT Offline Configuration Domain Model

## Scope

`EtherCATData` owns the UI-independent offline configuration values and
validation algorithms used by later Project and Workbench issues. This layer
does not persist a project, create a widget, parse ESI XML, scan a bus, or
define a controller protocol.

The model follows the information flow used by TwinCAT's EtherCAT Process Data
page: a Sync Manager exposes eligible RxPDO or TxPDO assignments, and each
selected PDO contributes ordered entries to the process image. RxPDO values
are master-to-slave outputs; TxPDO values are slave-to-master inputs.

The implementation does not copy TwinCAT files, assets, or proprietary data.
Its public values remain product-owned C++ types.

## Process Data values

`ProcessDataConfiguration` contains:

- `SyncManagerConfiguration` with stable ID, index, name, direction, enabled
  state, and an optional byte-size limit;
- `PdoConfiguration` with stable ID, index, direction, Sync Manager,
  selected/fixed/mandatory/default flags, optional predefined group, mapping
  support marker, and ordered entries;
- `PdoEntryConfiguration` with stable ID, object index/subindex, name,
  bit length, typed and raw data type, optional requested bit offset, mapping
  support marker, and explicit padding marker.

An entry offset of `-1` requests automatic bit packing. A non-negative value
requests an exact process-image bit offset. Field names containing `bit` use
bits; derived byte offsets and sizes use bytes.

`validateProcessDataConfiguration()` returns both issues and a deterministic
`ProcessImagePreview`. The preview separates inputs from outputs and records
the absolute bit offset, byte offset, bit-within-byte, width, source PDO, Sync
Manager, direction, and type of every selected entry.

The validator detects:

- duplicate or missing Sync Managers and disabled or wrong-direction use;
- a deselected mandatory PDO, duplicate PDO assignment, unsupported mapping,
  invalid PDO index, and empty selected PDO;
- non-positive entry width, fixed-width data-type mismatch, unknown types,
  duplicate object mapping, invalid explicit offset, and overlap;
- a selected mapping that exceeds the configured Sync Manager byte limit.

Warnings such as an unknown data type do not make the configuration invalid;
errors do. The raw type and configured bit length remain available so a later
ESI extension can preserve an unknown device description without inventing a
mapping.

## Startup values

`StartupConfiguration` contains ordered `StartupParameterConfiguration`
records. Each record has a stable ID, enabled state, explicit order,
transition, index/subindex, typed and raw data type, raw value, and comment.

`validateStartupConfiguration()` checks non-negative unique order for enabled
records, a non-empty transition, a non-zero object index, and raw-value size
for fixed-width common data types. Repeated writes to the same object remain
possible when their order differs; the order is part of the engineering
configuration.

No Mock or online value overwrites Startup automatically. The Workbench page
only changes Startup through explicit table, dialog, order, or ESI-default
actions, and every accepted action is undoable. The CoE Online Mock page now
follows the same path: a confirmed copy appends one new `PS` request through
the public Project service, never overwrites an existing request, and remains
undoable.

## Distributed Clocks values

`DcConfiguration` stores enabled state, mode name, AssignActivate, potential
reference-clock selection, and typed SYNC0/SYNC1 records. Every cycle and shift
field is named with the `Ns` suffix and is therefore always expressed in
nanoseconds.

`validateDcConfiguration()` checks:

- SYNC signals cannot be enabled while DC is disabled;
- enabled DC has a mode name;
- SYNC1 requires SYNC0;
- enabled cycle times are in the range 1 to 4294967295 ns;
- a shift stays within one positive or negative cycle.

The domain model does not convert display units. The Workbench DC page uses
nanoseconds directly for every cycle and shift field. Any later page that
displays us or ms must convert explicitly at its boundary and preserve the
nanosecond value.

## Ownership and next integration steps

The public configuration header is
`ethercatdata/offlineconfiguration.h`. Project-persisted adapter and binding
values are declared separately in
`ethercatdata/deviceadapterselection.h`. Both depend only on Qt Core and
existing `EtherCATData` values. CMake and qbs list the same source files.

`OfflineSlaveConfiguration` now owns these three values. EtherCATProject format
version 4 persists them together with `MasterConfiguration`. The master value
stores `unassigned`, `free-run`, or `distributed-clocks` plus a nanosecond
cycle period. The Project plugin rejects inconsistent timing values, migrates
versions 1 through 3 with exact recovery backups, and exposes checked
replacement commands through `ProjectService`. Each accepted replacement
enters the project's unified Undo/Redo stack.

Each offline slave can additionally persist the exact ESI SHA-256 and a
`DeviceAdapterProjectSelection`: adapter ID/version, adapter-content SHA-256,
Process Data profile, and canonical slot-sorted module assignments. These are
reproducible compiler inputs, not runtime state. A single master-level
`SemanticBindingArtifactReference` points to the compiler-produced mapping
artifact and the project-configuration digest it was built from. Runtime
Resource IDs, Process Image offsets, controller epochs, and package/runtime
generations are deliberately absent.

Any topology or compile-input change atomically invalidates the master binding
reference; the corresponding Undo restores both the old input and the old
reference. Display-only renames do not invalidate it. Versions 1 through 3
load every new version-4 value empty and never infer an adapter from vendor,
product, position, name, or a device-description ID.

The Workbench Process Data page consumes this API for SM/PDO selection, entry
editing, validation feedback, and process-image preview. The Startup page uses
the corresponding API for ordered CoE requests, ESI defaults, raw values, and
type validation. The DC page uses it for ESI/manual operation modes,
AssignActivate, SYNC0/SYNC1 timing, and the potential-reference-clock flag.
All three pages submit candidates through their domain validator and the
public Project service; widgets do not replace domain validation.

## Verification

The focused `EtherCATCore` contract suite covers:

- valid SM2 RxPDO/output and SM3 TxPDO/input mapping;
- automatic and explicit bit offsets and byte-size preview;
- mandatory, missing/wrong SM, unsupported, duplicate, width, overlap, and
  capacity errors;
- valid and invalid Startup order/value records;
- valid nanosecond DC configuration, invalid cycle, and shift range;
- value semantics for project adapter selections and semantic-binding
  references.

The qualified Qt 6.11.0 Release `EtherCATCore` run passes 28 tests. Project
integration has separate format, migration, service, and Undo/Redo coverage.
The editable Process Data, Startup, and DC pages have Workbench integration
coverage.
