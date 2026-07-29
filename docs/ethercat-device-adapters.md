# EtherCAT device adapter packages

## Layer boundary

Device adapter packages belong to the IDE engineering layer. They translate an
exact ESI-described slave into open, namespaced capabilities, semantic signals,
process-data profiles, module instances, and bounded control-action candidates.

They do not implement controller transport. `EtherCATProductApi` and future
controller Providers expose only controller-wide state and generic runtime
resources. Core, Workbench, Automation Gateway, and the controller runtime must
not add branches for a manufacturer, model, module class, or CiA 402 object.

The intended flow is:

```text
ESI identity and exact SHA-256
  -> adapter package selection
  -> process-data profile and module assignment
  -> semantic signal binding
  -> project safety policy and control envelope
  -> deterministic compiler
  -> opaque runtime resource catalog and immutable task package
```

An unknown device remains visible through its ESI and topology identity, but no
writable semantic signal or control action is inferred.

## Package registry

`EtherCATDeviceAdapters` loads versioned packages from
`ethercat/adapters/v1`. A package is selected only when all of the following
match:

- VendorId and ProductCode;
- the complete revision interval;
- exact ESI SHA-256;
- an explicitly selected or uniquely matching process-data profile; and
- for modular devices, every supplied slot and ModuleIdent assignment.

Required process-image bindings use direction, PDO index, object index,
SubIndex, physical type, and bit width. A missing or ambiguous required binding
is an error. Runtime byte and bit offsets are outputs of binding; packages do
not hard-code them.

Candidate, Mock-only, Unqualified, and Revoked packages cannot cross the
real-hardware gate. A real-hardware request also requires a Qualified package,
a verified signature, and explicit hardware permission.

The current file loader does not implement independent signature verification,
so it rejects any package that sets `signatureVerified` or
`realHardwareAllowed` to true. A JSON file cannot self-assert trust. Enabling
that path later requires a separate verifier that supplies those results
outside the package payload.

## Modular devices

A modular adapter retains the parent coupler identity separately from module
instances. Each module assignment carries:

- slot number;
- exact ModuleIdent;
- object-index offset; and
- PDO-index offset.

Slot-relative signal templates are expanded only after those values are known.
Stable signal IDs include the slot, so two equal modules cannot alias each
other. The parent identity alone is never used to guess an installed module.

## Control actions

A control action is deterministic binder/compiler input, not a sequence of
network requests issued by a Provider. It may contain only a finite list of:

- typed signal writes;
- masked feedback checks;
- absolute-limit feedback checks; and
- bounded delays.

Every wait has a timeout. Actions declare their resource dependencies,
controller/runtime preconditions, command TTL, exclusive-control requirement,
DC requirement, hold-to-run policy, and timeout/failure behavior.

Raw drive Controlword and mode-command signals are internal action resources.
They must not become generic editable UI fields. Candidate actions remain
disabled until process-data layout, engineering units, motion envelope,
disconnect handling, and real hardware have all been qualified.

## Bundled Candidate packages

### Solidot XB6-EC0002 revision 1

The parent adapter matches:

| Field | Value |
|---|---|
| VendorId | `0x00884443` |
| ProductCode | `0x000000b6` |
| Revision | `0x00000001` |
| ESI SHA-256 | `5b0bfbfffdfde1fd293589deb4a1c59f974aa79dcd9206ac0a695ab00f395bf7` |

The coupler Status word (`0xf100:01`) is read-only. The XML does not define the
meaning of Coupler Control bits, so `0xf200:01` is not exposed as a manual
control.

The first module templates cover the exact 16-channel digital modules:

| Module | ModuleIdent | Direction | Slot-relative object |
|---|---:|---|---|
| XB6-1600B | `0x00000629` | input | `0x6000:01..16` |
| XB6-1600A | `0x00000628` | input | `0x6000:01..16` |
| XB6-0016B(W) | `0x00000625` | output | `0x7000:01..16` |
| XB6-0016A | `0x00000624` | output | `0x7000:01..16` |

Input channels are read-only. Output channels remain Candidate-only with manual
control disabled and no asserted safe value: the ESI proves the Boolean PDO
shape but does not prove that `false` is safe for the attached machine.

The parent ESI has no DC mode. It does not grant FreeRun or DC capability to the
complete bus.

### Inovance SV630N one-axis revision 0x00010000

The adapter matches:

| Field | Value |
|---|---|
| VendorId | `0x00100000` |
| ProductCode | `0x000c0112` |
| Revision | `0x00010000` |
| ESI SHA-256 | `e6f39fd4e0f8801c83ec3ac796e138fe3ee1566cb94bb28285b93538fdb9e4a1` |

The speed-control Candidate profile selects RxPDO `0x1702` and TxPDO `0x1b04`.
Those PDOs provide Controlword, Statusword, mode command/display, target/actual
position, target/actual velocity, target/actual torque, error code, and
following error. Other mutually exclusive PDO variants are not silently
combined.

The ESI declares DC synchronization with AssignActivate `0x0300`; it does not
declare FreeRun support. Velocity and position engineering units and safe
motion limits are not present in the XML, so these values remain raw device
units and all motion actions remain disabled.

The Candidate action plans document the intended finite CiA 402 transitions and
feedback checks. Before they can be enabled, a project must supply a qualified
motion envelope and the new process-data package must pass real DC, WKC,
hold-to-run, TTL-expiry, disconnect, fault, and controlled-stop testing.

## Runtime binding boundary

The provider-neutral Runtime Resource catalog supplies current-epoch opaque
handles and typed read-only values. It does not yet prove which resource
implements an adapter package's semantic signal. The IDE must not join these
models by display name, ordinal, vendor/model branch, object index, or
process-image offset.

A later deterministic compiler issue must generate an immutable binding
manifest with stable semantic/component binding IDs, the resolved signal ID,
adapter identity/version/content hash, ESI hash, selected profile and module
assignment, canonical value type, and a layout digest. The controller catalog
must return a verifiable binding identity or mapping digest before Workbench
can label an opaque resource as an XB6 or SV630N signal. The join must also
match the complete runtime catalog epoch. Until then, a Product API adapter may
expose only unmapped read-only resources; it cannot authorize output control
or execute Candidate actions.

## Qualification boundary

The bundled packages are offline Candidate artifacts. They do not qualify:

- a physical XB6 module layout;
- a digital output safe state;
- a SV630N engineering-unit conversion;
- a safe speed or torque limit;
- a new SV630N PDO package;
- a Controlword write or actual motor movement; or
- any real-controller mutation.

Promotion to Qualified is a separate, evidence-bearing change after the generic
runtime resource catalog is available and headless hardware acceptance passes.
