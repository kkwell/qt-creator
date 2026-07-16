# EtherCAT ESI Device Repository

## Scope and ownership

`EtherCATDevicesPlugin` owns offline ESI parsing, immutable device
descriptions, content-addressed source storage, filtering, and asynchronous
index construction. It has no mode, widget, item model, project persistence,
network transport, or controller protocol. Workbench is the first plugin that
may present this data to users.

The plugin publishes one `DeviceRepositoryProvider` through the Qt Creator
object pool. Consumers use the exported interface in `EtherCATCore`; they do
not include parser or repository implementation headers.

## Repository layout

The product repository is rooted at:

```text
Core::ICore::userResourcePath()/ethercat/esi
├── repository.json
└── sources
    └── <lowercase-sha256>.xml
```

Imported XML is validated before it is stored. The exact bytes are retained
under their SHA-256 name, so unknown vendor extensions are not lost and equal
files are deduplicated. `repository.json` is a versioned, atomically replaced
metadata index containing the original source path and UTC import time. It is
not the authoritative copy of device semantics; all descriptions can be
rebuilt from the stored XML. A rebuild verifies that each file still matches
the SHA-256 encoded in its name and rejects tampered content.

Device `NodeId` values are deterministic UUID-v5 values derived from Vendor
ID, Product Code, and Revision Number. Renaming a type therefore does not
change its identity, while two revisions of one product remain distinct. If
newer source content describes an existing identity, its description remains
the selected revision after a persisted index rebuild.

## Parsed phase-1 data

The parser matches XML element local names and accepts both unnamespaced ESI
files and namespaced `EtherCATInfo` documents. An unfamiliar namespace is
reported as a warning instead of discarding otherwise valid data.

The current offline model extracts:

- Vendor ID, Product Code, Revision Number, localized name, type, and group;
- SyncManager address, size, control byte, enable state, and direction;
- RxPDO and TxPDO mapping, entries, indexes, subindexes, bit lengths, common
  EtherCAT/IEC data types, fixed/mandatory flags, and SyncManager assignment;
- CoE SDO-info, PDO-assignment, PDO-configuration, and complete-access flags;
- CoE startup transition, index, subindex, raw data, and comment;
- DC mode, AssignActivate, and literal Sync0/Sync1 cycle and shift values;
- source path, SHA-256, import time, warnings, and unsupported markers.

Malformed XML, a wrong root, a missing Vendor ID, a missing Product Code or
Revision Number, an empty Devices collection, and a device without usable
name/type text reject that source. A batch reports failures per source; it
does not silently create partial identities.

Modular `Modules`, `Module`, `Slots`, and `Slot` structures are retained only
in the original XML and explicitly mark the phase-1 description unsupported.
DC formula attributes are likewise reported because only the literal timing
value is evaluated. Unknown PDO data types remain available as raw names and
produce warnings.

## Asynchronous lifecycle

`importFiles()` and `rebuildIndex()` return provider-owned jobs in Pending
state. Work starts through the event loop, and file I/O, hashing, XML parsing,
and content-addressed source writes run on a worker thread. Only immutable
results return to the GUI thread; that thread swaps repository state and
emits reset/change notifications.

Repository jobs are serialized so imports and index replacement cannot race.
Cancellation is idempotent and is checked between source files. A job always
publishes at most one terminal result and is deleted through the event loop
after completion. Plugin shutdown cancels and joins outstanding workers
before removing the provider from the object pool.

`delayedInitialize()` starts the persisted-source rebuild, so plugin startup
does not parse an ESI library on the GUI thread. Rebuild replaces the in-memory
index atomically after all usable sources have been parsed.

## Deliberate phase-1 limits

- No ESI editor, device tree, details panel, or import dialog is owned here.
- No modular terminal composition is expanded yet.
- No vendor profile dictionary is converted into executable behavior.
- No DC formula is evaluated against a configured master cycle.
- No device is written into an EtherCAT project yet.
- No live scan, controller connection, ECPKG, or Zynq protocol is present.

These limits preserve a small offline contract for Workbench and later
plugins without coupling the repository to UI or controller implementation.
