# Embed Labs EtherCAT device adapter packages v1

This directory contains immutable upper-layer engineering adapter packages.
A package maps one exact ESI identity to semantic signals and bounded action
candidates. It does not implement controller transport, direct PDO or SDO
writes, or a controller-side vendor branch.

Only JSON files with
`schemaVersion: "embed-labs.device-adapter/v1"` are packages. The loader
rejects unknown, missing, malformed, duplicate, ambiguous, or mismatched
fields.

## Trust boundary

The bundled packages are offline `candidate` artifacts:

- `signatureVerified` is `false`;
- `realHardwareAllowed` is `false`;
- every manual output is disabled;
- every control action is disabled; and
- process-image offsets are resolved from the selected project/package, never
  copied from this resource.

The v1 file loader has no independent signature verifier and therefore rejects
any package that sets either trust Boolean to true. Package JSON cannot
self-assert qualification for real hardware.

Promotion to `qualified` requires a separate signed evidence-bearing change
after the generic runtime resource catalog and headless real-hardware tests
have passed.

## Top-level fields

Every package contains exactly:

- `schemaVersion`, `id`, `version`, `displayName`, and `description`;
- `qualification`, `matchPriority`, and exact `match`;
- open namespaced `capabilities`;
- immutable ESI `source` and `evidenceSha256`;
- `signatureVerified` and `realHardwareAllowed`;
- `signals`, `processDataProfiles`, `moduleProfiles`, and
  `controlActions`.

All numeric identities and object/PDO indexes are unsigned JSON integers.
SHA-256 values are 64 lower-case hexadecimal characters.

## Signals and bindings

A signal contains all of these fields:

`id`, `displayName`, `description`, `capabilities`, `direction`,
`access`, `required`, `bindings`, `value`, `safeValue`, and
`manualControl`.

Bindings are exact tuples of `kind`, `pdoDirection`, `pdoIndex`,
`objectIndex`, `subIndex`, `physicalType`, `bitWidth`,
`byteOrder`, and `slotRelative`.

For a modular device, every channel is listed explicitly. The signal ID keeps
only the `{slot}` placeholder. The resolver substitutes a validated
`DeviceModuleAssignment` and applies its object/PDO offsets. It never guesses
an installed module from the parent coupler identity.

When a project supplies an adapter ID, version, and content SHA-256, the
resolver accepts only that exact immutable package. These three fields are
all-or-nothing; a missing package and a content mismatch are separate errors.
Requests from older projects that omit all three fields retain automatic
identity and priority matching.

## Control actions

Actions are finite binder/compiler input, not Provider-side network command
sequences. A step kind is one of `write-signal`,
`wait-masked-equals`, `wait-absolute-at-most`, or `delay`.
Step values and masks are tagged as `invalid`, `literal`, or `parameter`.
Every step has a finite timeout.

Runtime conditions remain open namespaced conditions and are preserved by the
loader. A compiler or executor that cannot support one must reject the action
before dispatch. Missing parameters, missing required signals, zero-TTL enabled
actions, or unsupported steps are rejected. The bundled actions remain
disabled and have a zero TTL by design.

## Bundled scope

- `solidot-xb6-ec0002-rev1.adapter.json` covers the exact XB6 coupler,
  its RxPDO 0x16ff / TxPDO 0x1aff coupler profile, read-only raw CouplerState,
  disabled internal CouplerCtrl, and four exact 16-channel DI/DO module
  profiles.
- `inovance-sv630n-rev00010000.adapter.json` covers the exact
  RxPDO 0x1702 / TxPDO 0x1b04 DC profile in raw drive units and disabled,
  finite CiA 402 CSV preparation and stop candidates.

The XB6 ESI does not prove a machine-safe output value. The SV630N ESI does
not prove engineering-unit scaling or a safe motion envelope.
