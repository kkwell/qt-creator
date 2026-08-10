# EtherCAT device adapter packages

> Before changing Adapter loading or qualification, run
> `python3 scripts/ethercat_feature_locator.py show ethercat.adapters.catalog-authorization`.
> The machine-readable ownership map is
> `docs/ethercat-feature-locator.json`; its generated Chinese view is
> `docs/ethercat-feature-code-map.zh_CN.md`.

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

`EtherCATDeviceAdapters` recursively loads packages below `ethercat/adapters`.
The current parser accepts the explicit schemas `embed-labs.device-adapter/v1`,
`v2`, `v3`, and `v4`. The bundled production forms remain separated under
`adapters/v1`, `v2`, and `v3`; this tree does not install a production v4
manifest or v2 authorization. Existing production work therefore remains v3,
while v1/v2 are compatibility inputs and v4 is currently exercised with
dynamically signed test artifacts. A package is selected only when all of the
following match:

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
real-hardware gate. Package JSON still cannot self-assert
`signatureVerified` or `realHardwareAllowed`. After parsing, the repository
independently verifies signed authorization policy and per-adapter
authorization material from `ethercat/adapter-authorizations` against public
keys in `ethercat/adapter-authorization-trust`. Only an exact Qualified adapter
covered by accepted policy may receive verified, real-hardware-qualified state;
an incomplete, invalid, revoked, conflicting, or rolled-back authorization
fails closed and disables manual control.

## Device parameter definitions

Adapter v4 inherits the complete v3 contract and requires a
`parameterDefinitions` array. The array is bounded to 256 entries, strictly
ordered by unique canonical parameter ID, and every entry declares its display
metadata, engineering value kind and unit, exact constraint, required/default
policy, configured projection, and observed source. A configured projection is
either explicitly `project-only` or a fixed `coe-startup-sdo` at transition
`PS`. An observed source is either explicitly `unavailable` or a fixed
`coe-sdo-upload`. CoE bindings are typed, little-endian, reject inexact
engineering conversion, and the configured and observed bindings must be
identical when both exist.

The v4 manifest content digest and every parameter-definition digest use
separate SHA-256 domains over canonical JSON:

```text
SHA256("embed-labs.device-adapter/v4" || 0x00 || canonical manifest)
SHA256("embed-labs.device-parameter-definition/v1" || 0x00 || canonical definition)
```

Authorization v1 remains valid only for Adapter v3. Authorization v2 is valid
only for Adapter v4, uses the signature domain
`embed-labs.ethercat-device-adapter-authorization/v2`, and closes over the
exact v4 Adapter binding plus a strictly sorted list of
`{id, definitionSha256}` records. A changed, missing, reordered, or substituted
definition therefore invalidates the authorization even if the rest of the
Adapter identity is unchanged.

Core's `validateConfiguredDeviceParameters()` is the reusable qualification
gate for project values. It requires the exact ESI digest and complete Adapter
selection, including an existing process-data profile; Adapter v4;
`Qualified`; and both independently projected trust results,
`signatureVerified` and `realHardwareAllowed`. It then rejects malformed
definition closures, unknown or missing required IDs, kind mismatches, and
values outside the signed engineering constraints.

This contract is not an online parameter service. No production v4 Adapter is
currently installed, Product API does not yet provide observed parameter
evidence, and configured/observed/match UI and compiler projection are not yet
implemented. Non-empty project parameters therefore continue to fail closed
before compiler invocation. The installed SV630N Adapter remains v3 and its
motion actions remain disabled; v4 parsing or validation is not evidence of an
applied drive parameter or motor movement.

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

## Bundled device notes

The following identity and ESI observations originated with the earlier
Candidate packages. Current qualification and action enablement must be read
from the exact loaded v3 manifest plus its independently verified authorization,
not inferred from this historical narrative.

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

In the original Candidate package, input channels were read-only and output
manual control was disabled because the ESI proved the Boolean PDO shape but
did not prove that `false` was safe for the attached machine. The current v3
decision must come from its signed authorization and project safety evidence.

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

The original speed-control Candidate profile selected RxPDO `0x1702` and TxPDO
`0x1b04`. Those PDOs provide Controlword, Statusword, mode command/display,
target/actual position, target/actual velocity, target/actual torque, error
code, and following error. Other mutually exclusive PDO variants are not
silently combined.

The ESI declares DC synchronization with AssignActivate `0x0300`; it does not
declare FreeRun support. Velocity and position engineering units and safe
motion limits are not present in the XML, so these values remain raw device
units and all motion actions remain disabled.

Those Candidate action plans documented the intended finite CiA 402 transitions
and feedback checks. A current action may be enabled only when its exact v3
definition is qualified and the project supplies the required motion envelope
and evidence for DC, WKC, hold-to-run, TTL expiry, disconnect, fault, and
controlled stop.

## Runtime binding boundary

The provider-neutral Runtime Resource catalog supplies current-epoch opaque
handles and typed values. Signed semantic-binding and action-definition
artifacts now bind project device instances and adapter definitions to those
resources. Workbench may create a writable semantic context only after the
ECPKG signature, mapping digest, controller attestation, complete runtime epoch,
group policy and action qualification all match.

The IDE must still never join these models by display name, ordinal,
vendor/model branch, object index, process-image offset, or a stale ResourceId.
Output changes use the generic atomic output-transaction contract; Adapter
actions remain engineering/compiler data and do not become vendor commands in
ProductApi or CPU1.

## Qualification boundary

Authorization qualifies an exact Adapter artifact for the software gate; it
does not by itself prove the attached machine, wiring, units or motion envelope.
Current source and historical tests do not automatically qualify:

- a physical XB6 module layout;
- a digital output safe state for the attached machine;
- a SV630N engineering-unit conversion;
- a safe speed or torque limit;
- a new SV630N PDO package;
- a Controlword write or actual motor movement; or
- any real-controller mutation.

Each newly enabled output or motion action still requires evidence-bearing,
headless hardware acceptance against the exact Adapter, ESI, ECPKG, topology
and controller runtime epoch.
