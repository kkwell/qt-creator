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
3. The signature covers
   `embedlabs-ethercat-compiler-runtime-bundle-v1`, one NUL byte, and the exact
   canonical `manifest.json` bytes.
4. The installed tree is a closed set: every regular file has the signed path,
   size, mode and SHA-256; unexpected files, links, unsafe permissions and
   changes observed during validation fail closed.
5. The compiler runs from the verified complete tree. Copying only the small
   shell entrypoint breaks its relative `runtime/` lookup and is forbidden.
6. The Python executable and dependency environment are separately
   provisioned and fixed. User-site, `PYTHONPATH`, `PYTHONHOME`, loader
   injection variables and caller overrides must not select another runtime.
7. The complete installed tree is revalidated before every compiler process,
   and the provider must record a post-run drift check as operation evidence.

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

## Current integration gaps

- The legacy provider pins a single executable file; API-068 requires a
  verified complete runtime tree.
- API-068 intentionally does not supply the IDE's administrator-owned
  `ethercat-ide-compiler-provisioning-v1` profile.
- API-068 provisioning generates a complete
  `ethercat-ide-project-compiler-request-v1`; the current IDE file named
  `compile-inputs.json` is instead an
  `ethercat-ide-compiler-input-provisioning-v1` catalog. They are different
  contracts and must not overwrite one another.
- The bundle does not embed a product Python runtime or an offline dependency
  wheelhouse. A temporary development virtual environment is acceptance
  evidence only.
- The API-068 signing key is limited to this exact release. Future product
  releases need governed key rotation, revocation and recovery.

Close the active compiler-provisioning issue only after a clean installation
can discover and verify the runtime, generate inputs from the current
ProjectSnapshot, execute the complete compiler lifecycle, and recover the
same OperationId after restart.
