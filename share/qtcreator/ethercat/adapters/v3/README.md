# Device Adapter manifests v3

The v3 manifests are upper-layer IDE contracts. They keep the stable
`org.embedlabs.adapter.*` package identity separate from the controller
compiler Adapter named by `controllerAdapterTarget`.

The bundled XB6 rev1 and SV630N rev00010000 manifests are derived from the
API-038 handoff:

- `semantic-action-definitions-v1.json` supplies the signed action definition
  IDs, definition SHA-256 values, PDO profile IDs, qualification, parameters,
  and ordered steps.
- `semantic-binding-v2.json` supplies the exact controller Adapter identity,
  ESI identity, complete consistency groups, maximum TTL, recovery policy,
  access, and safe-value evidence.
- `compile_report.json` and the audited v2 manifests supply exact PDO/object
  bindings. Project-instance Resource IDs, positions, station addresses,
  process-image offsets, and numeric consistency-group IDs are deliberately
  excluded.

`failureDisposition` is an IDE-local conservative policy because API-038 does
not sign an equivalent field. Parameter step, origin, and default values are
also explicit IDE constraints, not fields from the signed action definition.

The files are installed as untrusted fixtures:
`signatureVerified` and `realHardwareAllowed` remain `false`. Runtime use must
independently verify the active production ECPKG, semantic-binding-v2 mapping
digest, signed action-definition companion, full controller epoch, and output
group policy before enabling a write. Format-v1 semantic bindings remain
read-only.

The whole `share/qtcreator/ethercat` directory is already copied and installed
recursively by both `share/qtcreator/CMakeLists.txt` and `share/share.qbs`, so
adding this directory requires no build-system source list change.
