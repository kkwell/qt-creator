# ISSUE-AI-CONTROLLER-001 Evidence

## Bound scope

The issue is bound exclusively to `ethercat-ai-controller/`. It adds no Qt
target, changes no CMake/qbs file, and does not modify the pre-existing root
`AGENTS.md`.

Acceptance scope:

1. audit Product API, ECPKG, ETIR, ESI, and Adapter state;
2. define the complete data/trust boundary;
3. define four versioned formats;
4. define controller-tools-v1 MCP/OpenAPI;
5. separate present capabilities from protocol gaps;
6. provide a mock controller and three device classes;
7. implement read-only Gateway and validation;
8. reject the five required unsafe/ambiguous cases;
9. perform no hardware/lease/scan/deploy/run; and
10. bind implementation and evidence in the knowledge graph.

## Verification command

```sh
python3 -m unittest discover -s tests -v
```

Qualified result before the final repository-wide check:

```text
Ran 42 tests in 0.757s

OK
```

Required failure-first cases passed:

- unknown device -> `CT001_UNKNOWN_DEVICE`;
- overlapping ESI/adapter scope -> `CT002_ESI_CONFLICT`;
- PDO/process-image over capacity -> `CT003_PDO_CAPACITY_EXCEEDED`;
- infeasible cycle/WCET budget -> `CT004_CYCLE_INFEASIBLE`; and
- unsafe velocity -> `CT005_DANGEROUS_MOTION`.

Additional gates cover missing timeouts, unknown fields, mock adapter escape,
contract/version parity, exact content hashes, read-only annotations,
immutable results, audit identity, idempotent reads, MCP lifecycle, protocol
errors, Origin rejection, loopback binding, and REST validation purity.

## Validator CLI

```sh
python3 -m controller_tools_v1 validate \
  mock/project/controller-project.json

python3 -m controller_tools_v1 validate \
  mock/intents/safe-servo-intent.json
```

Both checked-in examples are expected to return `valid: true`.

## Evidence classes

| Class | Status |
|---|---|
| Schema qualification | Passed locally |
| Offline semantic validation | Passed locally |
| Loopback REST/MCP | Passed locally |
| WSL Ubuntu | Not run; current workspace is macOS |
| `arm-linux-gnueabihf` build | Not applicable; no ARM program produced |
| ELF no-`INTERP` check | Not applicable; no ARM ELF produced |
| Product API controller | Not contacted |
| EtherCAT hardware | Not contacted |
| FPGA/CPU1 runtime | Not contacted |
| Scan/lease/deploy/run/motion | Not performed |

## Commit binding

The knowledge graph uses `commitRef: SELF` because a Git commit cannot contain
its own SHA without changing that SHA. The final report supplies the concrete
commit ID of the commit containing this evidence.

## Next issue

The only declared successor is `ISSUE-AI-CONTROLLER-002`: add an authenticated,
read-only Product API snapshot source behind the Gateway using loopback
controller simulation first. It must not add scan, lease, configuration,
deployment, task start, or motion.
