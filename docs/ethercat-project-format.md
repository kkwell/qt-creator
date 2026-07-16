# EtherCAT Offline Project

## Scope

`EtherCATProject` owns the current offline engineering-project lifecycle.
It integrates the `*.ecatproject` MIME type with Qt Creator's public
ProjectExplorer, document, wizard, and Save All APIs. It does not parse ESI,
persist PDO/DC/Startup models, scan a controller, or produce diagnostics. The
stage-5 Project revision can persist a checked list of offline slaves accepted
by another plugin; it still owns no scan state or comparison result.

The plugin has required plugin dependencies on `Core`, `ProjectExplorer`, and
`EtherCATCore`, and ordinary target dependencies on `EtherCATData`, `Utils`,
and Qt Widgets. It does not include an upstream plugin's Internal headers.

## ProjectExplorer and document ownership

ProjectExplorer owns the open-project list, startup project, close sequence,
session integration, and the built-in project-file watcher. Each
`EtherCATProject` owns one editable `EtherCATProjectDocument` registered with
DocumentManager without a second file watcher. This makes modified documents
participate in Save All while avoiding duplicate watches for the same project
file.

ProjectExplorer's public unload path does not save a custom document that has
no editor window. EtherCATProject therefore uses the public
`Project::aboutToSaveSettings` lifecycle signal to atomically save a modified
project before unload or shutdown. It uses the same checked save path as Save
All; no separate close-time serializer exists.

`ProjectService` mirrors ProjectManager add, remove, and startup-project
signals as immutable snapshots. Other EtherCAT plugins never receive a
Project, IDocument, QUndoStack, ProjectNode, or QModelIndex pointer.

All objects are GUI-thread-owned. There are no worker threads, timers,
futures, or cancellation paths in this plugin. During destruction, document
signals are disconnected before the undo stack and document are released.

## Version 1 format

The project is UTF-8 JSON with MIME type
`application/x-ethercat-project`. The original version-1 skeleton contains the
project, target, and master. Stage 5 adds an optional compatible `slaves`
array under the master; an older version-1 file without it loads as an empty
offline topology.

```json
{
    "format": "ethercat-project",
    "formatVersion": 1,
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
        "slaves": [
            {
                "id": "lowercase-uuid-without-braces",
                "name": "Mock Drive",
                "position": 0,
                "vendorId": 2,
                "productCode": 4096,
                "revisionNumber": 1,
                "serialNumber": 101,
                "alias": 0,
                "deviceDescriptionId": "optional-esi-device-node-id"
            }
        ]
    }
}
```

All project node IDs are required, non-null, and unique. Names are required and
cannot be empty after trimming. Slave positions must be non-negative and
unique within the master. Vendor ID and Product Code must be non-zero;
Revision, Serial Number, and Alias are explicit bounded unsigned values. The
optional device-description ID links to an immutable ESI repository entry but
does not embed ESI XML in the project.

Unknown or future format versions are rejected instead of being guessed.
Invalid JSON opens as an invalid, non-saveable project snapshot and adds a
ProjectExplorer error task; the damaged bytes are never overwritten.

Once a valid project is open, reload rejects a different project ID and keeps
the last valid in-memory snapshot. This prevents an external file replacement
from silently invalidating cross-plugin stable references.

PDO, Startup, DC, scan execution state, online state, and diagnostics remain
absent from version 1. Typed Process Data, Startup, and DC values and their
domain validators now exist in `EtherCATData`, but the next Project issue must
add an explicit format revision or compatible extension, checked service
commands, and Undo/Redo before they can be persisted.

## Migration and recovery

The first migration accepts the legacy version-0 shape with root-level `id`,
`name`, and `createdBy`. It preserves the project ID, creates stable target and
master IDs, marks the document modified, and requires an explicit save.

Before replacing a migrated source, the document copies the exact original
bytes to `<project>.v0.bak`. Existing backups are not overwritten; a numeric
suffix is selected. A backup failure aborts the save. The current file is
written through `Utils::FileSaver`, so a write or finalize failure leaves the
last valid project file intact.

Save As is disabled in this stage because Qt Creator's public Project API does
not expose an atomic way to retarget the already-open ProjectExplorer project.
Renaming the engineering project changes its display name, not its file path.
Automatic temporary-file saves are also disabled; explicit Save and Save All
remain atomic and are the supported persistence paths.

## Undo, save, and modified state

All configuration edits owned by this plugin use its single QUndoStack.
Project rename and replacement of one master's entire offline-slave list are
commands. The replacement is validated and normalized before it reaches the
stack, so a rejected, cancelled, or failed scan cannot partially mutate the
document. Pushing, undoing, and redoing update the immutable snapshot and
ProjectExplorer display name. Navigation, project-tree refresh, startup-project
switching, and snapshot reads do not modify the document.

The QUndoStack clean index and pending migration state are the only modified
sources. A successful atomic save marks the current stack index clean. Failed
saves leave both the undo history and modified state unchanged.

## New-project workflow

The `EtherCAT Engineering Project` platform-independent Qt Creator project
wizard creates one version-1 `*.ecatproject` file and opens it through
ProjectExplorer. The wizard uses current Qt Creator factory registration and
GeneratedFile attributes; it does not introduce a custom dialog framework.

## Project verification

The focused plugin test covers:

- metadata, required plugin dependencies, service registration, and wizard;
- format round trip, malformed JSON, unsupported versions, and invalid
  document behavior;
- rename, modified state, Undo/Redo, Save All registration, Save As rejection,
  atomic write failure, and successful save;
- offline-slave validation, deterministic position ordering, version-1
  persistence, service application, Undo/Redo, and malformed-array rejection;
- version-0 migration, exact recovery backup, and current-version rewrite;
- two real ProjectExplorer projects, active-project switching, service
  snapshots, modified-project close-save, close order, and cleanup.

macOS test runs use an isolated HOME because an earlier intentional crash-path
test can cause AppKit's saved-state restorer to display an unrelated modal
prompt on later headless runs. This changes no product behavior.
