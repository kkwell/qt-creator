# EtherCAT Offline Project

## Scope and ownership

`EtherCATProject` owns the local `*.ecatproject` engineering-project lifecycle.
It integrates Qt Creator's public ProjectExplorer, `IDocument`, wizard, Save
All, and project-file watcher APIs. It does not parse ESI XML, scan a
controller, produce diagnostics, or define a controller download format.

The plugin has required dependencies on `Core`, `ProjectExplorer`, and
`EtherCATCore`, and ordinary target dependencies on `EtherCATData`, `Utils`,
and Qt Widgets. No other plugin's `Internal` header crosses this boundary.

ProjectExplorer owns open projects, startup-project state, session integration,
and close sequencing. Each `EtherCATProject` owns one
`EtherCATProjectDocument`, one immutable `ProjectSnapshot`, and one
`QUndoStack`. The public `ProjectService` exposes only value snapshots and
checked commands; Project, document, stack, model, and index pointers remain
private.

All Project objects are GUI-thread-owned. This plugin has no worker thread,
timer, future, or cancellation path. Document signals are disconnected before
the document and undo stack are destroyed.

## Version 3 format

The current file is indented UTF-8 JSON with MIME type
`application/x-ethercat-project`, format name `ethercat-project`, and
`formatVersion` 3. Version 2 added per-slave Process Data, Startup, and DC
configuration. Version 3 adds the provider-neutral master timing mode and
cycle period needed to build a controller package. It is local editor data,
not ECPKG, ECFG, ETIR, a network message, or a TwinCAT project file.

The top-level shape is:

```json
{
    "format": "ethercat-project",
    "formatVersion": 3,
    "project": {
        "id": "lowercase-uuid-without-braces",
        "name": "Packaging Line",
        "createdBy": "Embed Labs 20.0.1"
    },
    "target": {
        "id": "lowercase-uuid-without-braces",
        "name": "Target Controller"
    },
    "master": {
        "id": "lowercase-uuid-without-braces",
        "name": "EtherCAT Master",
        "configuration": {
            "timingMode": "unassigned",
            "cyclePeriodNs": 0
        },
        "slaves": []
    }
}
```

`timingMode` is `unassigned`, `free-run`, or `distributed-clocks`. An
unassigned master must have a zero cycle. FreeRun and Distributed Clocks must
have a cycle from 1 through 4294967295 ns. The master setting expresses
engineering intent; a later adapter still validates it against controller
capabilities and the selected slaves before building or deploying a package.

Every slave has this structural data plus one required `configuration` object:

```json
{
    "id": "lowercase-uuid-without-braces",
    "name": "Drive",
    "position": 0,
    "vendorId": 2,
    "productCode": 4096,
    "revisionNumber": 1,
    "serialNumber": 101,
    "alias": 0,
    "deviceDescriptionId": "optional-esi-device-node-id",
    "configuration": {
        "processData": {
            "syncManagers": [],
            "pdos": []
        },
        "startup": {
            "parameters": []
        },
        "dc": {
            "enabled": false,
            "modeName": "",
            "assignActivate": 0,
            "sync0": {
                "enabled": false,
                "cycleTimeNs": 0,
                "shiftTimeNs": 0
            },
            "sync1": {
                "enabled": false,
                "cycleTimeNs": 0,
                "shiftTimeNs": 0
            },
            "potentialReferenceClock": false
        }
    }
}
```

Project, target, master, slave, Sync Manager, PDO, PDO entry, and Startup IDs
are required, non-null, and unique across the project. Display names never act
as identity. Slave positions are non-negative and unique. Vendor ID and
Product Code are non-zero. Revision, Serial Number, Alias, indexes, subindexes,
and sizes are range-checked before conversion to their C++ value types.

## Process Data records

Each Sync Manager record contains:

| Field | Type and meaning |
|---|---|
| `id` | Required stable ID |
| `index` | Non-negative integer |
| `name` | String |
| `direction` | `unknown`, `master-to-slave`, or `slave-to-master` |
| `enabled` | Boolean |
| `sizeLimitBytes` | Non-negative byte limit; zero means no declared limit |

Each PDO record contains:

| Field | Type and meaning |
|---|---|
| `id` | Required stable ID |
| `index` | Unsigned 16-bit object index |
| `name` | String |
| `direction` | `rx` for master output or `tx` for master input |
| `syncManager` | Assigned SM index; `-1` is unassigned |
| `selected` | Current offline assignment state |
| `fixed` | ESI marks the assignment fixed |
| `mandatory` | ESI marks the assignment mandatory |
| `defaultSelected` | ESI default assignment marker |
| `mappingSupported` | Current implementation supports the mapping |
| `predefinedGroup` | Optional ESI grouping string |
| `entries` | Ordered PDO entry array |

Each PDO entry contains stable `id`, unsigned 16-bit `index`, unsigned 8-bit
`subIndex`, `name`, positive `bitLength`, `dataType`, `rawDataType`,
`requestedBitOffset`, `mappingSupported`, and `padding`. Offset `-1` means
automatic layout; a non-negative number requests an exact bit offset. JSON
integer values that exceed the exact IEEE-754 range are rejected.

`dataType` uses one of:

```text
unknown
boolean
integer8            unsigned-integer8
integer16           unsigned-integer16
integer32           unsigned-integer32
integer64           unsigned-integer64
real32              real64
visible-string      octet-string
```

The loader invokes `validateProcessDataConfiguration()`. Missing or
wrong-direction SM assignments, duplicate mappings, invalid widths, overlap,
unsupported mappings, mandatory deselection, and SM capacity overflow reject
the whole file. Warnings such as an explicitly preserved unknown data type do
not reject it. Process-image preview is derived and is not persisted.

## Startup and DC records

Each Startup parameter contains stable `id`, Boolean `enabled`, integer
`order`, `transition`, unsigned 16-bit `index`, unsigned 8-bit `subIndex`,
`dataType`, `rawDataType`, `rawValueHex`, and `comment`. `rawValueHex` is an
even-length ASCII hexadecimal string; it preserves bytes without numeric or
endianness reinterpretation. The domain validator enforces order, transition,
object index, and fixed-width raw-value rules.

The DC object contains `enabled`, `modeName`, unsigned 16-bit
`assignActivate`, `sync0`, `sync1`, and `potentialReferenceClock`. Each signal
contains `enabled`, `cycleTimeNs`, and `shiftTimeNs`. The `Ns` suffix is the
unit contract. The loader validates mode presence, SYNC1/SYNC0 dependency,
cycle range, and shift range.

## Public editing and Undo/Redo

`ProjectService` provides checked editing commands for:

- one Target or Master display name by stable node ID;
- one master's offline slave list;
- one slave's complete Process Data configuration;
- one slave's complete Startup configuration;
- one slave's complete DC configuration.

Structural-node rename is intentionally limited to Target and Master. Project
rename remains a separate command, while Slave names remain part of the
offline-slave value. Names are trimmed; empty names, unknown IDs, Project IDs,
and Slave IDs are rejected. Other commands reject unknown
project/master/slave IDs, invalid domain data, and null or reused configuration
IDs. No-op changes return success without creating a command. Accepted changes
enter the same project `QUndoStack`. Undo and redo publish a new immutable
snapshot and update the document's modified state. Sorting, navigation,
selection, refresh, and snapshot reads do not create undo commands or mark the
document modified.

The QUndoStack clean index and pending migration state are the only modified
sources. A successful atomic save marks the stack clean. Failed validation or
save leaves the current snapshot, history, source file, and modified state
unchanged.

## Migration, corruption, and recovery

Version 1 contains the same structural project and optional slave list but no
per-slave `configuration`. It loads with empty Process Data and Startup values
and disabled DC, is marked migrated/modified, and is rewritten as version 3
only after explicit Save or Save All. Before replacement, the exact source
bytes are copied to `<project>.v1.bak`; existing backups receive a numeric
suffix and are never overwritten.

Version 2 preserves all per-slave configuration but has no master timing
configuration. It loads with an unassigned master, is marked
migrated/modified, and is rewritten as version 3 only after explicit Save or
Save All. The exact version-2 bytes are first copied to
`<project>.v2.bak`.

The legacy version-0 root shape with `id`, `name`, and `createdBy` remains
supported. It preserves the project ID, creates stable target/master IDs, and
uses the same explicit-save flow with `<project>.v0.bak` recovery.

Invalid JSON, unsupported versions, missing current-format configuration
objects, inconsistent master timing, invalid scalar types/ranges, invalid raw
hex, duplicate stable IDs, and domain-invalid configuration are rejected. An
initially damaged file opens as an invalid, non-saveable snapshot and adds a
ProjectExplorer error task; its bytes are never overwritten. Reload of an
already valid project also rejects a changed project ID and keeps the last
valid in-memory snapshot.

The current file is written through `Utils::FileSaver`. A temporary-file or
finalization failure leaves the last valid file intact. Save As remains
disabled because the current public Project API cannot atomically retarget an
already-open ProjectExplorer project. Automatic temporary saves are disabled;
explicit Save and Save All are the supported persistence paths.

## New-project and close behavior

The `EtherCAT Engineering Project` wizard creates one version-3 file and opens
it through ProjectExplorer. The wizard uses current Qt Creator factory and
GeneratedFile APIs.

ProjectExplorer's public unload path does not save a custom document without
an editor. EtherCATProject therefore listens to public
`Project::aboutToSaveSettings` and runs the same checked atomic save before
unload or shutdown. There is no second close-time serializer.

## Verification

The focused Project suite covers:

- metadata, dependencies, service registration, and wizard discovery;
- version-3 structural, master-cycle, and slave-configuration round trips;
- malformed JSON, unsupported versions, missing configuration, duplicate IDs,
  invalid raw hex, and domain-invalid PDO mapping;
- Project and Target/Master rename, topology replacement, Process Data,
  Startup, and DC command validation plus Undo/Redo;
- Save All registration, Save As rejection, atomic write failure, and success;
- exact version-0, version-1, and version-2 migration backups;
- two real ProjectExplorer projects, startup-project switching, close-save,
  signal publication, close order, and cleanup.

The qualified Qt 6.11.0 Release run passes 12 tests. macOS runs use an isolated
HOME and settings path so prior AppKit saved state cannot introduce an
unrelated modal prompt.
