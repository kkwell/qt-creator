# Natural Language to Runtime Data Boundaries

## Non-realtime control plane

The AI control plane is outside the 125 microsecond loop. It may propose data,
but it never owns a cyclic frame, CPU1 process-image pointer, FPGA register,
or raw SDO/PDO write.

The complete intended flow is:

```text
Untrusted natural language
  -> ControlIntent v1
  -> topology snapshot plus ESI repository
  -> NormalizedDeviceModel v1
  -> AdapterManifest v1 selection
  -> ControllerProject v1
  -> deterministic compiler input
  -> ETIR plus static-analysis report
  -> signed ECPKG
  -> approval and lease-bound deployment transaction
  -> CPU0 package manager
  -> CPU1 immutable activated task
  -> FPGA scheduled EtherCAT data path
  -> versioned state, process data, alarms, and diagnostics
```

Only the first four contract types, offline validators, and read-only mock
observations are implemented by `ISSUE-AI-CONTROLLER-001`.

## Boundary records and trust

| Boundary | Producer | Consumer | Trust on arrival | Required gate |
|---|---|---|---|---|
| Natural language | User/AI client | Intent generator | Untrusted text | Hash, actor, policy context |
| ControlIntent | AI client/generator | Binder/compiler | Candidate | Schema, resource, graph, timeout, safety-envelope checks |
| Topology snapshot | Controller provider or import | Binder | Controller/import evidence | ControllerId, BootId, hash, freshness |
| ESI bytes | Engineer/vendor repository | ESI parser | Untrusted file | XML validity, exact identity, content hash |
| NormalizedDeviceModel | Standard/ESI/vendor adapter | Binder/validator | Candidate or qualified model | Schema, ESI hash, model consistency |
| AdapterManifest | Standard/vendor/AI | Adapter registry | Candidate | Exact identity, ESI allow-list, deterministic tests, review, signature |
| ControllerProject | Project generator | Compiler | Bound candidate | Complete topology, PDO/SDO/DC, PI, WKC, cycle and policy validation |
| ETIR | Deterministic compiler | Package builder/CPU1 VM | Compiler output | Bytecode verifier, WCET, resource declarations, reproducible hash |
| ECPKG | Package builder | CPU0 package manager | Signed candidate | Signature, controller capability, A/B stage, approval and lease |
| Activated task | CPU0 atomic activation | CPU1 | Immutable approved artifact | Safe-state precondition and atomic generation switch |
| Runtime data | CPU1/FPGA | CPU0/Gateway/AI | Authoritative observation | Boot/task generation, timestamp, sequence, quality |

## ControlIntent v1

`ControlIntent` preserves the user's UTF-8 text and SHA-256, requested cycle,
exact resource bindings, safe outputs, motion envelopes, approval decision,
and an `ecl-v1` bounded control-flow graph.

The graph:

- has at most 4,096 nodes;
- uses a closed operation enumeration;
- requires declared resources and a positive timeout on every node;
- represents repetition with a bounded `loopCount`;
- contains no source text for shell, file, network, memory, or register access;
  and
- is not executable until a future deterministic compiler qualifies it.

Natural language is never passed to CPU1.

## NormalizedDeviceModel v1

The NDM is the only semantic source the binder may use for a slave. It
contains:

- exact identity and ESI source/hash;
- mailbox and protocol capabilities;
- SyncManager and FMMU requirements;
- RxPDO/TxPDO and object-dictionary semantics;
- DC Sync0/Sync1 limits and supported cycles;
- watchdog and startup SDO rules;
- CiA402 axes, modes, and required objects;
- process-image variable layout;
- safe outputs;
- vendor-private initialization status; and
- diagnostics and recovery rules.

`missing-information` is a first-class status. The binder must not synthesize
unknown vendor register values or sequencing.

## AdapterManifest v1

The registry selection order is:

```text
standard protocol
  -> exact ESI
  -> verified vendor adapter
  -> AI candidate
  -> deterministic validation
  -> engineer information or approval when still incomplete
```

Matching uses identity scope, exact ESI SHA-256, priority, version, and
manifest content hash. Equal-priority or overlapping incompatible ESI scopes
are conflicts, not tie-break opportunities.

States are:

- `candidate`: offline proposal only;
- `mock-only`: deterministic fixture only;
- `verified`: eligible only with evidence and signature;
- `revoked`: never selected.

No non-verified state may set `realHardwareAllowed`.

## ControllerProject v1

The project binds one immutable controller capability snapshot to:

- exact slave order, station addresses, identity, ESI, model, and adapter;
- a concrete input/output process image;
- bounded startup SDOs and their adapter owner;
- FreeRun or DC timing, frames, wire time, master WCET, task WCET, margin,
  and expected WKC;
- one ControlIntent version;
- safety-policy identifiers; and
- offline/build/package/deployment state.

The project is not the existing Qt `*.ecatproject` v2 format. A future,
separately tested transformer must preserve stable IDs and produce a
traceability report between the two.

## Compiler and runtime boundary

A future compiler may accept only a schema-valid, semantically valid,
fully-bound project. It must reject:

- undeclared slave, PDO, SDO, variable, axis, or function-block access;
- recursion, dynamic allocation, unbounded loops, or missing timeouts;
- process images or frames beyond controller capacity;
- WCET plus communication budget beyond the selected cycle;
- unexpected WKC;
- unsafe motion ranges; and
- unresolved or unverified adapter behavior.

Its output must include reproducible ETIR bytes, source/project/compiler
hashes, WCET, PI sizes, frame plan, WKC, supported controller capabilities,
and validation diagnostics.

CPU0 may stage and atomically activate a signed package. CPU1 consumes only
the verified immutable generation and retains task ownership if the AI or
CPU0 network disconnects. FPGA scheduling remains independent of the AI.

## Deployment transaction boundary

No deployment transaction exists in this issue. The future transaction must
be:

```text
Prepare -> Validate -> Approve -> Sign -> Upload -> Stage
        -> Safe-state precondition -> Activate -> Confirm
        -> Start -> Monitor -> ControlledStop or Rollback
```

Every mutating request needs:

- authenticated user and AI client;
- ControllerId and current BootId;
- controller-authoritative exclusive lease;
- OperationId and idempotency key;
- before-state and expected generation;
- parameter/content hash;
- policy decision and approval evidence; and
- a durable result record.

Retrying an OperationId must return the original transaction result, not
repeat the side effect.

## Runtime observation boundary

Read-only data returned to an AI must carry enough identity to reject stale
interpretation:

- ControllerId and BootId;
- session and provider generation;
- active package/task generation;
- timestamp and sequence;
- variable quality/freshness;
- expected and actual WKC;
- DC lock/difference when applicable; and
- raw plus normalized diagnostic codes.

An AI connection loss can never be the sole implementation of a safety
function. Emergency stop and STO remain hardware or certified safety paths.
