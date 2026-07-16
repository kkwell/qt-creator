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

No Mock or online value overwrites Startup automatically. A future Workbench
command must make any copy into Startup explicit and undoable.

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

The domain model does not convert display units. A later page may display ns,
us, or ms, but it must convert explicitly at its boundary and preserve the
nanosecond value.

## Ownership and next integration steps

The public header is `ethercatdata/offlineconfiguration.h`. It depends only on
Qt Core and existing `EtherCATData` values. CMake and qbs list the same source
files.

This issue deliberately does not add the model to `ProjectSnapshot` or the
version-1 project file. The next Project issue must add versioned persistence,
corruption checks, migration behavior, checked service commands, and one
Undo/Redo path before Workbench may edit these values.

The later Workbench issue must consume this API for the TwinCAT-inspired
Process Data, Startup, and DC pages. It must not duplicate offset, overlap,
width, Startup, or DC validation inside table widgets.

## Verification

The focused `EtherCATCore` contract suite covers:

- valid SM2 RxPDO/output and SM3 TxPDO/input mapping;
- automatic and explicit bit offsets and byte-size preview;
- mandatory, missing/wrong SM, unsupported, duplicate, width, overlap, and
  capacity errors;
- valid and invalid Startup order/value records;
- valid nanosecond DC configuration, invalid cycle, and shift range.

The suite passes 16 tests on the qualified Qt 6.11.0 Release test build. This
is domain-contract evidence only; editable pages and project-file persistence
are not yet claimed.
