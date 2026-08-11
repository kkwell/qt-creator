# EtherCAT Offline Project

> The source of truth for the current writer version is
> `src/plugins/ethercatproject/ethercatprojectconstants.h`. Use
> `python3 scripts/ethercat_feature_locator.py show ethercat.project.model-format`
> for the owning files and tests; the generated overview is
> `docs/ethercat-feature-code-map.zh_CN.md`.

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

## Version 8 format

The current file is indented UTF-8 JSON with MIME type
`application/x-ethercat-project`, format name `ethercat-project`, and
`formatVersion` 8. Version 2 added per-slave Process Data, Startup, and DC
configuration. Version 3 adds the provider-neutral master timing mode and
cycle period needed to build a controller package. Version 4 adds reproducible
ESI/device-adapter selection and one compiler-produced semantic-binding
artifact reference. Version 5 adds bounded manual-control envelopes and retains
the binding reference. Version 6 adds the canonical `projectDeviceBindings`
field to a present artifact reference. Version 7 adds the configured station
address used by fresh topology and compiler evidence. Version 8 adds bounded,
canonical project-owned device-parameter intent under each slave's
`configuration.deviceParameters`. It is local editor data, not ECPKG, ECFG,
ETIR, a network message, observed controller evidence, or a TwinCAT project
file.

The top-level shape is:

```json
{
    "format": "ethercat-project",
    "formatVersion": 8,
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
        "semanticBindingArtifact": {
            "artifactId": "binding/com.embedlabs.line/1",
            "artifactSha256": "64-lowercase-hex-characters",
            "projectConfigurationSha256": "64-lowercase-hex-characters",
            "projectDeviceBindings": []
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
    "stationAddress": 4097,
    "deviceDescriptionId": "optional-esi-device-node-id",
    "esiSha256": "64-lowercase-hex-characters",
    "adapterSelection": {
        "adapterId": "com.embedlabs.vendor.device",
        "adapterVersion": "1.2.3",
        "adapterContentSha256": "64-lowercase-hex-characters",
        "processDataProfileId": "default",
        "moduleAssignments": [
            {
                "slot": 0,
                "moduleIdent": 4097,
                "objectIndexOffset": 0,
                "pdoIndexOffset": 0
            }
        ]
    },
    "manualControlEnvelope": {
        "enabled": false,
        "signalEnvelopes": [],
        "actionEnvelopes": []
    },
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
        },
        "deviceParameters": {
            "values": [
                {
                    "parameterId": "parameter.example",
                    "value": {
                        "kind": "unsigned-integer",
                        "unsignedInteger": "1048576"
                    }
                }
            ]
        }
    }
}
```

Project, target, master, slave, Sync Manager, PDO, PDO entry, and Startup IDs
are required, non-null, and unique across the project. Display names never act
as identity. Slave positions are non-negative and unique. Vendor ID and
Product Code are non-zero. Revision, Serial Number, Alias, indexes, subindexes,
and sizes are range-checked before conversion to their C++ value types.

`esiSha256` and `adapterSelection` are optional, but a non-empty adapter
selection requires the ESI digest. Adapter ID, version, content digest, and
Process Data profile are an all-or-nothing value. Every SHA-256 string is
exactly 64 lowercase hexadecimal characters. Module slots are non-negative
and unique, `moduleIdent` is non-zero, and both index offsets fit unsigned
16-bit values. Modules are normalized to ascending slot order before entering
the project and are serialized in that canonical order.

`deviceParameters` is required inside every version-8 slave `configuration`
object and contains exactly one `values` array. Each entry contains exactly
`parameterId` and `value`. A parameter ID is 1 through 256 ASCII characters:
the first character is an ASCII letter or digit, and subsequent characters
may additionally be `.`, `_`, or `-`. IDs are unique and serialized in strict
ascending order. One slave may contain at most 256 values and one project at
most 4096 values.

The canonical `EngineeringValue` object uses exactly one of these shapes:

```json
{"kind":"boolean","boolean":true}
{"kind":"signed-integer","signedInteger":"-1000"}
{"kind":"unsigned-integer","unsignedInteger":"1000"}
{"kind":"exact-rational","exactRational":{"numerator":"1","denominator":"2"}}
{"kind":"enumeration","enumerationName":"profile.csv"}
```

Integer and rational components are decimal strings so JSON floating-point
conversion cannot change them. Leading zeroes, `+`, and other non-canonical
spellings are rejected. A rational must have a positive denominator, coprime
numerator and denominator, and canonical zero `0/1`. Enumeration names use the
same bounded ASCII identifier grammar as parameter IDs. Unknown fields,
unsupported kinds, duplicate or unsorted IDs, and non-canonical values reject
the complete project.

A non-empty device-parameter configuration is valid only on a slave with an
exact ESI SHA-256 and complete Adapter selection. The checked
`setDeviceParameterConfiguration()` command also receives the caller's
expected ESI digest and expected complete Adapter selection as a value token;
it rejects the edit if either no longer matches the current slave. Changing
the ESI or Adapter clears the device parameters. Parameter edits clear the
master semantic-binding artifact in the same undo command, and Undo restores
both values atomically.

These values are engineering intent only. Adapter v4 now defines a signed,
bounded parameter-definition closure, and Core can validate project values
against its exact ESI, complete Adapter/Profile selection, Qualified state,
independent signature/hardware authorization results, required IDs, kinds, and
engineering constraints. The version 8 persistence path itself still performs
only the generic structural validation above; it does not turn saved values
into qualified device operations.

The production SV630N v4 Adapter and Authorization v2 are installed, and the
Workbench can compare Project intent with separately bound, session-scoped
Product API v1.16 observed evidence. That evidence is never persisted into
`ProjectSnapshot`. Parameters are still absent from the signed compiler
contract, so every compile request containing a non-empty device-parameter
configuration fails closed before compiler invocation.

Applying a current real bus preserves existing parameters only when the slave
identity and ESI remain exact and the complete saved v4 Adapter selection is
re-resolved with the same ID, version, content digest, profile, and modules. A
new slave or one without a saved selection never adopts v4 automatically; it
may only use the existing unambiguous v3 selection path. If any binding changes,
the normal Project safety rule clears the parameters. No device-parameter value
in this format, nor the presence of the v4 contract or an Observed match,
authorizes deployment, SDO download, or motor motion.

`semanticBindingArtifact` is optional and appears only once at master level.
It is an immutable reference containing a non-empty trimmed artifact ID, the
artifact digest, and the digest of the project configuration the compiler
bound. The project does not persist runtime Resource IDs, Process Image
offsets, controller BootId, package/runtime generations, or any other runtime
epoch. Those values remain controller-session data and must be resolved from
the verified compiler artifact and current runtime catalog.

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
- one slave's complete DC configuration;
- one slave's atomic ESI digest and device-adapter selection;
- one slave's complete device-parameter configuration, guarded by the expected
  exact ESI digest and Adapter selection;
- the master's single semantic-binding artifact reference.

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

The binding artifact is fail-closed against its compile inputs. Changing the
master timing configuration, replacing the topology, changing Process Data,
Startup, DC, device parameters, ESI, or adapter selection clears the reference
in the same undo command. Undo restores both the previous compile input and its
previous artifact reference atomically. Display-only Project, Target, and
Master renames preserve the artifact.

The QUndoStack clean index and pending migration state are the only modified
sources. A successful atomic save marks the stack clean. Failed validation or
save leaves the current snapshot, history, source file, and modified state
unchanged.

## Migration, corruption, and recovery

Version 1 contains the same structural project and optional slave list but no
per-slave `configuration`. It loads with empty Process Data and Startup values
and disabled DC, is marked migrated/modified, and is rewritten as version 8
only after explicit Save or Save All. Before replacement, the exact source
bytes are copied to `<project>.v1.bak`; existing backups receive a numeric
suffix and are never overwritten.

Version 2 preserves all per-slave configuration but has no master timing
configuration. It loads with an unassigned master, is marked
migrated/modified, and is rewritten as version 8 only after explicit Save or
Save All. The exact version-2 bytes are first copied to
`<project>.v2.bak`.

Version 3 preserves its master timing, Process Data, Startup, and DC values
exactly. It loads with empty ESI digests, adapter selections, and binding
artifact reference; the loader never guesses an adapter from identity, name,
position, or device-description ID. It is marked migrated/modified and is
rewritten as version 8 only after explicit Save or Save All, after first
copying the exact source bytes to `<project>.v3.bak`.

Version 4 preserves exact ESI and adapter selection, but its pre-instance
semantic artifact reference is not retained as writable evidence. Version 5
adds manual-control envelopes and retains the artifact reference without
project-device bindings. Version 6 requires the `projectDeviceBindings` field
when an artifact reference is present, but has no configured station address.
Version 7 preserves its configured station addresses but has no
`deviceParameters` object. Versions 4 through 7 load with empty device
parameters, are marked migrated/modified, are backed up with their matching
`.v4.bak`, `.v5.bak`, `.v6.bak`, or `.v7.bak` suffix, and are rewritten as
version 8 only after explicit Save or Save All.

The legacy version-0 root shape with `id`, `name`, and `createdBy` remains
supported. It preserves the project ID, creates stable target/master IDs, and
uses the same explicit-save flow with `<project>.v0.bak` recovery.

Invalid JSON, unsupported versions, missing current-format configuration
objects, inconsistent master timing, invalid scalar types/ranges, invalid raw
hex, non-lowercase or partial SHA-256 values, partial adapter/artifact objects,
invalid or duplicate module slots, zero ModuleIdent, offset overflow,
duplicate stable IDs, and domain-invalid configuration are rejected. An
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

The `EtherCAT Engineering Project` wizard creates one version-8 file and opens
it through ProjectExplorer. The wizard uses current Qt Creator factory and
GeneratedFile APIs.

ProjectExplorer's public unload path does not save a custom document without
an editor. EtherCATProject therefore listens to public
`Project::aboutToSaveSettings` and runs the same checked atomic save before
unload or shutdown. There is no second close-time serializer.

## Verification

The focused Project suite covers:

- metadata, dependencies, service registration, and wizard discovery;
- current-format structural, master-cycle, slave-configuration, ESI/adapter,
  device-parameter, and binding-artifact round trips;
- malformed JSON, unsupported versions, missing configuration, duplicate IDs,
  invalid raw hex/digests, partial selections, invalid module assignments, and
  domain-invalid PDO mapping;
- Project and Target/Master rename, topology replacement, Process Data,
  Startup, DC, adapter selection, expected-token device-parameter edits,
  binding invalidation, and atomic Undo/Redo;
- Save All registration, Save As rejection, atomic write failure, and success;
- exact version-0 through version-7 migration and recovery behavior;
- two real ProjectExplorer projects, startup-project switching, close-save,
  signal publication, close order, and cleanup.

The focused suite, not a historical fixed test count, is the current evidence
source. macOS runs use an isolated HOME and settings path so prior AppKit saved
state cannot introduce an unrelated modal prompt.
