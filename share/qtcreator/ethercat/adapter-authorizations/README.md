# Signed device-adapter authorization v1

This directory ships the production policy and exact XB6/SV630N
authorizations admitted by ISSUE-API-054. An embedded V3 adapter manifest must
still keep `signatureVerified` and `realHardwareAllowed` set to `false`. The
IDE raises those two values only in memory after independently verifying the
files described here.

Policy files use the suffix `.policy.json`; their detached raw 64-byte Ed25519
signature uses the same basename with `.policy.sig`. Authorization files use
`.authorization.json` and `.authorization.sig`. Symbolic links are forbidden.
Documents are exact ASCII
`kvell-json-ascii-sorted-compact-lf-v1`: sorted compact JSON, one final LF, no
duplicate keys or extra fields.

The policy format string is
`embed-labs.ethercat-device-adapter-authorization-policy-v1`. Its signature is
over the following bytes:

`embed-labs.ethercat-device-adapter-authorization-policy/v1` + NUL + canonical JSON

The authorization format string is
`embed-labs.ethercat-device-adapter-authorization-v1`. Its signature is over:

`embed-labs.ethercat-device-adapter-authorization/v1` + NUL + canonical JSON

The closed camelCase layouts are frozen by
`schemas/device-adapter-authorization-policy-v1.schema.json` and
`schemas/device-adapter-authorization-v1.schema.json`. Arrays that represent
sets must be unique and sorted. Policies have a minimum revision of 1, bind
each delegated signer to exact adapter IDs and `allow`/`deny` decisions, and
carry signer and authorization revocations. An accepted revision cannot roll
back during the repository lifetime, and the same revision cannot change its
canonical SHA-256 or root key ID. The compiled minimum policy revision is 1.
There is deliberately no writable policy ledger in v1: policies are signed
application resources, so a product release that retires an older revision
must raise that compiled minimum. This is the documented cross-process floor;
the in-memory pin protects reloads within one process.

An authorization binds an exact V3 upper adapter ID, version, content SHA-256,
qualification, match identity, ESI, controller-side lower adapter identity and
hashes, complete PDO/DC profile list, complete action list and definition
hashes, and the manifest evidence SHA-256. A valid `deny`, any mismatch,
duplicate/conflict, revocation, unknown key, damaged signature, wrong domain,
or noncanonical document leaves both runtime trust flags false and produces an
authorization diagnostic without disabling ordinary offline adapter loading.
