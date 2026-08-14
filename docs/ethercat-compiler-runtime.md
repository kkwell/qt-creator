# EtherCAT external compiler runtime

This document records the product boundary for the external ProjectSnapshot
compiler. It is the starting point for compiler delivery, discovery and
runtime failures; do not rediscover the full plugin tree first.

## Ownership and call path

The IDE owns the engineering workflow and immutable operation evidence. The
external runtime owns deterministic `compile`, `query`, `finalize` and
`verify` execution. A production private key never enters either process.

```text
ProjectSnapshot + fresh topology + provisioned inputs
  -> RuntimePackageCompilerProjectRequestBuilder
  -> RuntimePackageCompilerPreparationCoordinator
  -> RuntimePackageCompilerProvider
  -> signed external compiler runtime
  -> detached signer/HSM
  -> verified ECPKG + activation proof
```

The provider contract is in `ethercatcore`. The installed implementation and
durable operation store are in `ethercatprojectcompiler`. Workbench consumes
the public preparation service; it must not execute Python or parse compiler
private files directly.

## Trust boundary

An installed runtime is trusted only after all of these checks succeed before
starting a process:

1. A raw 32-byte Ed25519 public key is provisioned outside the received
   archive.
2. The release identity binds the exact bundle ID, version and manifest
   SHA-256. A release test key is not a product-wide trust root.
3. The signature covers the exact versioned domain
   (`embedlabs-ethercat-compiler-runtime-bundle-v1` or the governed v2 domain),
   one NUL byte, and the exact canonical `manifest.json` bytes. A v1-domain
   signature cannot authenticate a v2 manifest.
4. The installed tree is a closed set: every regular file has the signed path,
   size, mode and SHA-256; unexpected files, links, unsafe permissions and
   changes observed during validation fail closed.
5. The compiler runs from the verified complete tree. Copying only the small
   shell entrypoint breaks its relative `runtime/` lookup and is forbidden.
6. The Python executable and dependency environment are separately
   provisioned and fixed. User-site, `PYTHONPATH`, `PYTHONHOME`, loader
   injection variables and caller overrides must not select another runtime.
7. The complete installed tree is revalidated before every compiler process
   and again before any process output is decoded or accepted. A failed
   post-run check rejects the result and preserves its operation directory as
   unaccepted diagnostic evidence.

The verifier defends against a damaged or malicious archive and installation
changes made by another unprivileged user. Every directory from the filesystem
root to the bundle root must be owned by root or the IDE's effective user,
must not be group/world writable and, on macOS, must not carry an extended
ACL that grants access. Restrictive deny-only ACL entries, including the
standard macOS `everyone deny delete` entry, are permitted. The bundle itself
has the same ownership, mode, link and ACL gates.

The IDE's own effective user, root, the kernel and a filesystem that ignores
POSIX ownership are outside this boundary. `validateCurrent()` is a strict
snapshot check, not an immutable executable handle. The provider must call it
synchronously immediately before `Utils::Process::start()`; callers must not
describe a user-writable path as resistant to a compromised same-user process.

## API-068 Mac acceptance

The API-068 archive is a release-specific evidence bundle, not a future global
trust root.

| Identity | Value |
| --- | --- |
| Bundle ID / version | `org.embedlabs.ethercat.project-compiler` / `1.0.0` |
| Archive bytes | `3748965` |
| Archive SHA-256 | `bc469c984951fa8ced59f284d99664d5eafaee7a13b6f3fbe705fbf7fe4c57e3` |
| Manifest SHA-256 | `23508c36437a18b87e9c3bb2360242399ded50dff526817eb5ff3e48450b8066` |
| Runtime key ID | `f10f0ac737194a19edfe2401c24ee945589b502d1c04d819ec7fac42735d367e` |
| Compiler entrypoint SHA-256 | `2fb898a97d341ea62437e8b7b4f82c0bbb696355287f6831f16ac5eafd8280c3` |
| Compiler request schema SHA-256 | `77443e0aeb051ff0d42a570a481456dac5106a6985da782c067b5623b68447fb` |

On 2026-08-09 the Mac acceptance used CPython 3.11.15 with the exact five
declared dependency versions. Archive verification, atomic installation,
installed-tree verification and the private-key-free canonical self-test all
passed. The self-test regenerated the three-device compile request, reached
`awaiting_signature`, finalized with the detached response, verified the
production signature, recovered the finalized ledger state and reproduced
the reference ECPKG byte for byte. It explicitly reported
`hardware_accessed=false`.

This proves the external compiler runtime and its golden contract. It does not
prove IDE discovery, current-project input generation, controller deployment
or real hardware operation.

## API-078 compiler-v2 boundary

The verified API-078 delivery archive has SHA-256
`407bc0424d8ce64e3664c5e4140a7b55812e119e31f55e012f0159bd70c036b3`.
Its Mac compiler runtime archive has SHA-256
`f977e58d11e1316c73e762e72855826696252d3c1bccf58d35c1be58bdd82ca8`,
manifest SHA-256
`bd4d19fed901c5d1feea0055b48a95adfc2c7017542afd7bbe2a09f818f83713`,
and contract `ethercat-ide-project-compiler` version 2. The IDE accepts that
identity separately from the frozen v1 identity, encodes
`ethercat-ide-project-compiler-request-v2`, checks v2 result identity, and
requires all seven companion identities declared by the signed compiler
manifest to match the installed Python runtime profile.

Compiler v2 adds a content-addressed parameter-contract bundle and a strictly
ordered per-device parameter projection. Each item binds its ID, signed
definition SHA-256, integer engineering value, and either a matching fixed
Product API v1.16 evidence record or JSON `null`. Project-only parameters must
not claim device evidence. The operation store materializes and revalidates the
parameter bundle, and compile-recovery payload v3 preserves the complete typed
projection while retaining v1/v2 decoding compatibility.

This delivery is explicitly `production-signed-motion-disabled` and records
`motion_section_9_emitted=false`. The Windows API-077 cfg7705 package is a
frozen, signed golden with Section 9; it was not produced by the delivered Mac
runtime. No other API-077-capable signed Mac runtime exists in the audited
delivery. Consequently this integration advances deterministic parameter
compilation only. It must not be described as an IDE path that rebuilds or
executes manual motion.

## Product runtime bootstrap

The product plugin reads the administrator-owned
`ethercat/compiler/runtime-expectations.json` before `provisioning.json` or
operation-store initialization. It is exact canonical ASCII JSON with format
`ethercat-ide-compiler-runtime-expectations-v1`, format version 1, and a closed
set of 18 fields. The fields bind the compiler root, version, manifest,
external release key and exact provisioning-profile SHA-256, plus the Python
companion root, installed-runtime root, version, bundle, manifest, external
release key, Python executable, installed-tree and portable-identity SHA-256.

Every digest is supplied externally. The IDE never derives a missing Python
executable, installed-tree or portable-identity expectation from the received
runtime's own identity file. The expectation file and two independent raw32
release keys remain outside all signed trees. The compiler, companion and
Python roots are mutually disjoint, and the writable operation root is a
sibling rather than an ancestor or descendant of any signed root.

Trust inputs require canonical local paths, safe leaves and ancestors, stable
metadata and exact bytes. Before becoming available, the provider also
requires `provisioning.json` to have the expected exact SHA-256, name the
signed compiler entrypoint by its exact path and use the contract version in
the compiler bundle. The provisioning digest binds its contract ID, schema
digest and production key without duplicating or freezing a future v2
compiler-contract shape in this expectation format.

Missing or invalid expectations register one unavailable provider. Product
startup never falls back to the legacy single-file constructor, `PATH`, a
system Python or a partially verified tree. Bootstrap is read-only: it does
not install a runtime, execute an installer, create the expectation file,
generate `provisioning.json` or initialize an operation store after failure.

## Current integration gaps

- API-068 intentionally does not supply the IDE's administrator-owned
  runtime expectation or `ethercat-ide-compiler-provisioning-v1` profile.
- API-068 provisioning generates a complete
  `ethercat-ide-project-compiler-request-v1`; the current IDE file named
  `compile-inputs.json` is instead an
  `ethercat-ide-compiler-input-provisioning-v1` catalog. They are different
  contracts and must not overwrite one another.
- The Core wire codec accepts only the exact contract ID/version pairs
  `ethercat-ide-project-compiler-contract-v1`/`1` and
  `ethercat-ide-project-compiler`/`2`. V1 canonical bytes and their golden SHA
  are unchanged; v1 cannot carry a parameter bundle or device parameters.
  Compile/finalize result formats must match the request contract. The schema
  bundle digest remains bound by exact provisioning, Provider, bootstrap, and
  operation-store equality instead of a codec constant.
- Non-empty explicit Project Startup SDOs remain denied. The Project model
  does not yet carry the compiler's complete-access, timeout, retry, failure,
  persistence and power-cycle semantics, so the IDE cannot invent defaults.
- A usable product installation still needs the exact v2 compiler tree,
  relocatable companion, runtime expectations, `provisioning.json`, and the
  current project's `compile-inputs.json`. API-078 does not supply a
  motion-enabled runtime. A new immutable signed delivery must include the
  API-077 verifier/qualification closed set and prove deterministic Section 9
  emission before manual motion integration can proceed.
- The API-068 signing key is limited to this exact release. Future product
  releases need governed key rotation, revocation and recovery.

## API-069 Python runtime boundary

API-069 supplies a signed offline CPython 3.11.9 companion, seven locked
wheels and an atomic installer. The Mac verified its external Ed25519 key,
archive signature, 25-entry closed set, wheel records, Apple installer signer
and an atomically installed companion tree. This remains supply-chain evidence,
not a usable IDE runtime.

The native acceptance correctly stopped because the exact Python 3.11.9 host
framework is absent. API-069 can only obtain it by running a privileged
Python.org system package that also installs global links and may update shell
profiles. That side effect is outside the IDE bootstrap boundary and was not
authorized or executed. Product integration therefore waits for API-070: a
signed, relocatable macOS arm64 runtime that installs wholly under its chosen
versioned root without root access or system/profile changes.

The corrected API-073 v1.0.4 companion is prepared on Windows but has not yet
been transferred and accepted into real macOS arm64 companion and runtime
roots. Its archive, manifest and external release key do not establish the
three installed-runtime expectations. Those values must come from independent
Mac acceptance and be entered by the administrator. Until all 18 fields and
both installed trees validate, the product provider remains unavailable. The
offline bootstrap tests do not access a controller or prove hardware motion.

Close the active compiler-provisioning issue only after a clean installation
can discover and verify the runtime, generate inputs from the current
ProjectSnapshot, execute the complete compiler lifecycle, and recover the
same OperationId after restart.
