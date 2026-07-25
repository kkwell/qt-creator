# Support and Semantic Limits

## ISSUE-AI-CONTROLLER-001 result

| Area | Supported now | Evidence class |
|---|---|---|
| Four data formats | Draft 2020-12 schemas plus semantic validation | Offline |
| MCP | Initialize, session, tools/list, tools/call, JSON response mode | Loopback mock |
| REST/OpenAPI | Nine tool mappings, OpenAPI 3.1.1 | Loopback mock |
| Controller | One immutable mock controller | Static fixture |
| Digital I/O | 8 DO/8 DI model; channel 1 bound in example | Static fixture |
| Analog I/O | 4 AO/4 AI signed 16-bit model | Static fixture |
| CiA402 | One mock axis, standard objects, bounded intent | Static fixture |
| Adapter registry | Three exact-ESI `mock-only` manifests | Offline |
| Failure gates | Unknown, ESI conflict, PDO, cycle, motion | Automated tests |
| Audit | Returned fields and in-memory bounded ring | Mock only |

## Not supported

- real controller discovery or authentication;
- Product API connection, refresh, or live provider projection;
- control lease, heartbeat, or approval acquisition;
- EtherCAT bus scan or physical port graph;
- applying topology to an offline project;
- raw or constrained SDO/PDO writes;
- full ESI object dictionary, modular devices, DC formulas, or vendor profiles;
- production device adapter signing/verification service;
- ECL compiler, ETIR, simulator, WCET analyzer, or CPU1 bytecode VM;
- ECPKG construction, signing, upload, stage, accept, activate, or rollback;
- process-variable streaming or persistent alarm replay;
- motion, task start, stop, or any other state-changing tool;
- OAuth, TLS termination, persistent audit, policy database, or multi-tenant
  idempotency storage; and
- WSL, ARM `arm-linux-gnueabihf`, ELF `INTERP`, CPU1, FPGA, or hardware
  verification.

## Device qualification limits

The three mock devices prove format coverage, not universal hardware support.
A real device remains output-disabled when any of the following applies:

- ESI is missing, malformed, incomplete, or has an unapproved hash;
- identity has no exact adapter match;
- two adapters overlap ambiguously;
- a vendor-private initialization step is undocumented;
- object meaning, unit, scaling, or safe value is unknown;
- selected PDO layout or SM/FMMU constraints fail;
- requested cycle is outside device/controller limits;
- CiA402 mode/object/scaling is incomplete; or
- adapter evidence/signature is absent, expired, or revoked.

The system reports the missing field and recovery action. It never guesses a
register, SDO value, mode, scaling, or state-machine transition.
