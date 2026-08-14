# EtherCAT AI Controller Contract Lab

`ISSUE-AI-CONTROLLER-001` creates a self-contained, offline reference
implementation for the future Controller AI Gateway. It defines the boundary
between an AI request and a deterministic controller task without adding any
path to real EtherCAT hardware.

The directory contains:

- versioned JSON Schema contracts for `ControlIntent`,
  `NormalizedDeviceModel`, `AdapterManifest`, and `ControllerProject`;
- a `controller-tools-v1` OpenAPI 3.1.1 description and matching MCP tool
  catalogue;
- a dependency-free Python validator and semantic safety gates;
- an MCP 2025-11-25 Streamable HTTP skeleton and REST read-only facade;
- one mock controller with digital I/O, analog I/O, and CiA402 servo devices;
- failure-first tests for unknown devices, ESI conflicts, PDO capacity,
  infeasible cycles, and unsafe motion parameters; and
- an issue/evidence knowledge graph.

## Safety boundary

This implementation is deliberately mock-only.

- It opens no Product API, EtherCAT, raw Ethernet, SDO, PDO, FPGA, CPU1, or
  hardware connection.
- It has no lease, scan, configuration, deployment, activation, start,
  motion, shell, raw-memory, or raw-register tool.
- It never compiles or executes ETIR and never creates or uploads ECPKG.
- The HTTP server refuses non-loopback bind addresses.
- Validation of a candidate adapter does not qualify it for real hardware.

Only immutable mock data and pure validation results are exposed. Future
state-changing tools require a separate issue with authentication, a
controller-authoritative lease, policy approval, idempotency storage, audit
persistence, and a Product API adapter.

## Versions

| Contract | Version |
|---|---|
| Gateway API | `controller-tools/v1` |
| Data API | `controller.embed-labs.dev/v1` |
| MCP protocol | `2025-11-25` |
| OpenAPI | `3.1.1` |
| JSON Schema | Draft `2020-12` |
| Restricted source label | `ecl-v1` (format only; no compiler in this issue) |

## Offline verification

From this directory:

```sh
python3 -m unittest discover -s tests -v
python3 -m controller_tools_v1 validate mock/project/controller-project.json
python3 -m controller_tools_v1 validate mock/intents/safe-servo-intent.json
```

The optional local mock server is:

```sh
python3 -m controller_tools_v1 serve --host 127.0.0.1 --port 8765
```

It serves:

- MCP Streamable HTTP at `POST /mcp`;
- REST under `/api/controller-tools/v1`;
- OpenAPI at `/api/controller-tools/v1/openapi.json`; and
- a local health response at `/healthz`.

`GET /mcp` returns `405` because this minimal server does not implement an SSE
stream. MCP clients initialize a session, send `notifications/initialized`,
then use `tools/list` and `tools/call`.

## Directory map

```text
api/                 MCP tool catalogue and OpenAPI contract
controller_tools_v1/ Dependency-free validator and read-only gateway
docs/                Audit, boundary, protocol, and evidence records
knowledge-graph/     ISSUE binding, capabilities, evidence, and next edge
mock/                Controller, NDM, Adapter, Project, and Intent fixtures
schemas/v1/          Canonical versioned JSON Schema contracts
tests/               Offline and loopback-only regression tests
```

See [existing-system-audit.md](docs/existing-system-audit.md) for the audited
Qt Creator/Product API baseline and [data-boundaries.md](docs/data-boundaries.md)
for the complete natural-language-to-runtime trust boundary.
