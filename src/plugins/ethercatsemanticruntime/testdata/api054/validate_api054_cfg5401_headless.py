#!/usr/bin/env python3
"""Fail-closed headless controller acceptance for API-054 cfg5401.

The default mode performs only local package and reference-client checks.  A
real controller connection requires all of ``--execute``, ``--deploy`` and the
exact confirmation string.  The hardware path verifies a production-signed
ECPKG, scans the actual topology, deploys it, starts DC, and exercises only
the qualified XB6 digital-output action.  It intentionally never emits an
SV630N action or motion command.

This validates the controller/API layer.  It is not a substitute for the
separate in-IDE provisioned compiler and UI acceptance gates.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import hashlib
import importlib.util
import json
import os
import secrets
import subprocess
import sys
import tempfile
import time
import types
import zipfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[5]
PHASE_B_ROOT = ROOT / "build/ethercat-handoff/api054-phaseb/extracted"
API067_ROOT = ROOT / "build/ethercat-handoff/api067/extracted"
DEFAULT_PACKAGE = PHASE_B_ROOT / "three-slave-api054-project-cfg5401.ecpkg"
DEFAULT_PUBLIC_KEY = (
    PHASE_B_ROOT
    / "artifacts/compiler-trust"
    / "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6.pub"
)
DEFAULT_REFERENCE_CLIENT = (
    API067_ROOT / "igh_osless/tools/product_api_client.py"
)
DEFAULT_REFERENCE_SUPPORT = PHASE_B_ROOT / "sources/igh_osless/tools"
DEFAULT_CAPACITY_CONTRACT = (
    PHASE_B_ROOT / "sources/igh_osless/contracts/product-capacity-v2.json"
)

EXPECTED_PACKAGE_SHA = (
    "b0bb1aa15c2e1eaa9c65c520c6d6a9cfdeec6a54bdf16282ee31e28c9bd92078"
)
EXPECTED_CLIENT_SHA = (
    "aeb939c19aa507d16dba9a97ce99c001af8b68fc2f94d3b8d44fd1311344a233"
)
EXPECTED_CAPACITY_MODULE_SHA = (
    "1576d6d52399c96479dd105eaad757dd5f06c989c06307ccbd9b5645e400caa9"
)
EXPECTED_CAPACITY_CONTRACT_SHA = (
    "7f01b03d616a2bd9f2a0f9e875930688590768baf0d814c909d57cc9a20f2278"
)
EXPECTED_KEY_ID = (
    "eceffa53d8903e70e4e317c066a2a1de8cf58a616bb8337a6f4dc9e7f4c10ac6"
)
EXPECTED_MAPPING_SHA = (
    "1a53290755ebe02136982694ee0c7ae487313d4492c26e1e4bebd42800afde43"
)
EXPECTED_CONFIG_ID = 5401
EXPECTED_CATALOG_REVISION = 0x1A0B4E3588236C68
EXPECTED_TOPOLOGY_IDENTITY = 0x697EAF5137AD0DA5
EXPECTED_XB6_GROUP = 0x80C4EF09
EXPECTED_MEMBERS = (
    "manifest.json",
    "capability.bin",
    "configuration.ecfg",
    "runtime.erun",
    "compile_report.json",
    "semantic-action-definitions-v1.json",
    "manifest.sig",
)
EXPECTED_IDENTITIES = (
    (0, 0x1001, 0x00884443, 0x000000B6, 0x00000001, 0),
    (1, 0x1002, 0x00100000, 0x000C0112, 0x00010000, 11),
    (2, 0x1003, 0x00100000, 0x000C0112, 0x00010000, 10),
)
EXECUTION_CONFIRMATION = "API054_CFG5401_DC_XB6_MANUAL_OUTPUT"
SV_ACTION_PREFIX = "org.embedlabs.inovance.sv630n.action."
XB6_SET_ACTION = "org.embedlabs.solidot.xb6.action.set-digital-outputs"
XB6_CLEAR_ACTION = "org.embedlabs.solidot.xb6.action.clear-digital-outputs"


class GateFailure(RuntimeError):
    """A package, topology, protocol, or safety gate failed closed."""


class Evidence:
    def __init__(self, path: Path, mode: str) -> None:
        self.path = path
        self.document: dict[str, Any] = {
            "format": "embed-labs-api054-cfg5401-headless-validation-v1",
            "mode": mode,
            "started_utc": self.now(),
            "events": [],
            "result": "running",
        }

    @staticmethod
    def now() -> str:
        return dt.datetime.now(dt.timezone.utc).isoformat()

    def add(self, step: str, status: str, **details: Any) -> None:
        self.document["events"].append(
            {"time_utc": self.now(), "step": step, "status": status,
             **jsonable(details)}
        )

    def finish(self, result: str, **details: Any) -> None:
        self.document["finished_utc"] = self.now()
        self.document["result"] = result
        self.document.update(jsonable(details))

    def write(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(self.path.suffix + ".tmp")
        temporary.write_text(
            json.dumps(self.document, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, self.path)


def jsonable(value: Any) -> Any:
    if dataclasses.is_dataclass(value):
        return jsonable(dataclasses.asdict(value))
    if isinstance(value, dict):
        return {str(key): jsonable(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [jsonable(item) for item in value]
    if isinstance(value, bytes):
        return value.hex()
    if isinstance(value, Path):
        return str(value)
    return value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise GateFailure(message)


def parse_int(value: str) -> int:
    return int(value, 0)


def as_int(value: int | str) -> int:
    return int(value, 0) if isinstance(value, str) else value


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical_json(value: Any) -> bytes:
    return (
        json.dumps(value, sort_keys=True, separators=(",", ":"),
                   ensure_ascii=True).encode("ascii")
        + b"\n"
    )


def verify_ed25519(public_key: bytes, manifest: bytes, signature: bytes) -> None:
    require(len(public_key) == 32, "production public key is not raw Ed25519")
    require(len(signature) == 64, "manifest.sig is not raw Ed25519")
    # RFC 8410 SubjectPublicKeyInfo prefix for raw Ed25519 public keys.
    spki = bytes.fromhex("302a300506032b6570032100") + public_key
    with tempfile.TemporaryDirectory(prefix="api054-signature-") as directory:
        root = Path(directory)
        key_path = root / "key.der"
        manifest_path = root / "manifest.json"
        signature_path = root / "manifest.sig"
        key_path.write_bytes(spki)
        manifest_path.write_bytes(manifest)
        signature_path.write_bytes(signature)
        result = subprocess.run(
            [
                "openssl", "pkeyutl", "-verify", "-pubin", "-keyform",
                "DER", "-inkey", str(key_path), "-rawin", "-in",
                str(manifest_path), "-sigfile", str(signature_path),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
    require(
        result.returncode == 0,
        "production Ed25519 signature verification failed: "
        + (result.stderr.strip() or result.stdout.strip()),
    )


def package_action(
    plans: list[dict[str, Any]], action_id: str
) -> dict[str, Any]:
    matches = [plan for plan in plans if plan.get("action_definition_id") == action_id]
    require(len(matches) == 1, f"signed action is not unique: {action_id}")
    return matches[0]


def validate_xb6_action(
    action: dict[str, Any], binding_by_id: dict[str, dict[str, Any]],
    parameterized: bool,
) -> tuple[list[dict[str, Any]], dict[int, int]]:
    require(action.get("enabled") is True, "XB6 action is not enabled")
    require(action.get("qualification") == "qualified",
            "XB6 action is not qualified")
    require(action.get("disabled_reason") is None,
            "qualified XB6 action has a disabled reason")
    groups = action.get("consistency_groups")
    require(isinstance(groups, list) and len(groups) == 1,
            "XB6 action does not define one consistency group")
    group = groups[0]
    require(
        as_int(group.get("consistency_group_id")) == EXPECTED_XB6_GROUP
        and group.get("max_ttl_cycles") == 1000
        and group.get("recovery_policy") == "hold_safe",
        "XB6 action policy is not the signed HOLD_SAFE/1000 policy",
    )
    required = action.get("required_bindings")
    steps = action.get("steps")
    require(isinstance(required, list) and len(required) == 16,
            "XB6 action does not contain all 16 output bindings")
    require(isinstance(steps, list) and len(steps) == 1
            and steps[0].get("kind") == "write_group",
            "XB6 action is not an atomic output group")
    step = steps[0]
    require(as_int(step.get("consistency_group_id")) == EXPECTED_XB6_GROUP,
            "XB6 action step has the wrong consistency group")
    assignments = step.get("assignments")
    require(isinstance(assignments, list) and len(assignments) == 16,
            "XB6 action does not write all channels")
    required_by_semantic = {
        value["semantic_binding_id"]: value for value in required
    }
    bindings: list[dict[str, Any]] = []
    channels: dict[int, int] = {}
    for assignment in assignments:
        semantic_id = assignment.get("semantic_binding_id")
        plan_binding = required_by_semantic.get(semantic_id)
        signed = binding_by_id.get(semantic_id)
        require(plan_binding is not None and signed is not None,
                "XB6 action assignment has no signed semantic binding")
        resource_id = as_int(signed["resource_id"])
        require(
            as_int(plan_binding["resource_id"]) == resource_id
            and signed.get("primitive") == "bool"
            and signed.get("bit_width") == 1
            and signed.get("direction") == "output"
            and signed.get("access") == "read_write"
            and signed.get("safe_value_declared") is True
            and signed.get("safe_value") == 0
            and as_int(signed["consistency_group_id"]) == EXPECTED_XB6_GROUP,
            "XB6 binding is not a signed safe writable boolean output",
        )
        source = assignment.get("value_source")
        require(isinstance(source, dict), "XB6 assignment has no value source")
        if parameterized:
            parameter = source.get("parameter_id", "")
            require(source.get("kind") == "parameter" and
                    parameter.startswith("do") and parameter[2:].isdigit(),
                    "XB6 set action is not parameterized by do0..do15")
            channel = int(parameter[2:])
            require(0 <= channel < 16,
                    "XB6 output parameter is outside do0..do15")
            channels[resource_id] = channel
        else:
            require(source == {"kind": "constant", "value": 0},
                    "XB6 clear action is not an all-zero atomic assignment")
        bindings.append(signed)
    require(len({as_int(value["resource_id"]) for value in bindings}) == 16,
            "XB6 action has duplicate resource bindings")
    if parameterized:
        require(set(channels.values()) == set(range(16)),
                "XB6 set action does not bind each DO channel exactly once")
    return bindings, channels


def load_package(args: argparse.Namespace) -> dict[str, Any]:
    package_path = args.package.resolve()
    public_key_path = args.public_key.resolve()
    require(package_path.is_file(), f"ECPKG does not exist: {package_path}")
    require(public_key_path.is_file(), f"public key does not exist: {public_key_path}")
    package_bytes = package_path.read_bytes()
    package_sha = sha256(package_bytes)
    require(package_sha == args.expected_package_sha,
            f"ECPKG SHA-256 mismatch: {package_sha}")

    with zipfile.ZipFile(package_path) as archive:
        require(tuple(archive.namelist()) == EXPECTED_MEMBERS,
                "ECPKG v2 member order/set differs from the signed contract")
        require(not archive.comment, "ECPKG archive comment must be empty")
        for info in archive.infolist():
            require(info.compress_type == zipfile.ZIP_STORED,
                    f"ECPKG member is compressed: {info.filename}")
            require(not info.extra,
                    f"ECPKG member has ZIP extra fields: {info.filename}")
            require(info.date_time == (1980, 1, 1, 0, 0, 0),
                    f"ECPKG member timestamp is not reproducible: {info.filename}")
            require(info.external_attr >> 16 == 0o100644,
                    f"ECPKG member mode is not 0644: {info.filename}")
        payloads = {name: archive.read(name) for name in EXPECTED_MEMBERS}

    manifest_bytes = payloads["manifest.json"]
    manifest = json.loads(manifest_bytes)
    require(manifest_bytes == canonical_json(manifest),
            "manifest JSON is not canonical signed JSON")
    require(manifest.get("format") == "ethercat-ecpkg"
            and manifest.get("format_version") == 2
            and manifest.get("production") is True,
            "package is not a production ECPKG v2")
    require(
        manifest.get("signature") == {
            "algorithm": "ed25519", "key_id": EXPECTED_KEY_ID,
            "signature_file": "manifest.sig",
        },
        "production signing descriptor differs from the expected trust key",
    )
    records = manifest.get("payloads")
    expected_payload_names = EXPECTED_MEMBERS[1:-1]
    require(isinstance(records, list)
            and tuple(record.get("name") for record in records)
            == expected_payload_names,
            "manifest payload records are not complete and ordered")
    for record in records:
        name = record["name"]
        data = payloads[name]
        require(len(data) == record.get("bytes")
                and sha256(data) == record.get("sha256"),
                f"payload digest/size mismatch: {name}")

    config = manifest.get("configuration")
    require(
        isinstance(config, dict)
        and config.get("configuration_id") == EXPECTED_CONFIG_ID
        and config.get("configured_cycle_ns") == 125000
        and config.get("expected_wkc") == 11
        and config.get("dc_record_count") == 2
        and config.get("effective_timing_mode") == "dc"
        and config.get("requested_timing_mode") == "dc"
        and config.get("slave_count") == 3,
        "manifest does not declare the exact cfg5401 DC topology",
    )
    key = public_key_path.read_bytes()
    require(sha256(key) == EXPECTED_KEY_ID,
            "public key does not match the production key ID")
    verify_ed25519(key, manifest_bytes, payloads["manifest.sig"])

    report = json.loads(payloads["compile_report.json"])
    semantic = report.get("semantic_binding_manifest")
    require(isinstance(semantic, dict),
            "compile report has no semantic binding manifest")
    mapping_sha = sha256(canonical_json(semantic))
    require(mapping_sha == args.expected_mapping_sha,
            f"semantic mapping SHA mismatch: {mapping_sha}")
    signed_semantic = manifest.get("semantic_binding")
    require(
        isinstance(signed_semantic, dict)
        and signed_semantic.get("format_version") == 2
        and signed_semantic.get("artifact_sha256") == mapping_sha
        and signed_semantic.get("binding_count") == 56
        and signed_semantic.get("action_definition_count") == 5
        and signed_semantic.get("catalog_revision") == EXPECTED_CATALOG_REVISION
        and signed_semantic.get("topology_identity") == EXPECTED_TOPOLOGY_IDENTITY
        and report.get("semantic_binding_manifest_sha256") == mapping_sha,
        "signed semantic binding metadata differs from cfg5401",
    )
    action_definitions = payloads["semantic-action-definitions-v1.json"]
    companion = report.get("semantic_action_definitions_manifest")
    require(
        isinstance(companion, dict)
        and companion.get("format")
            == "ethercat-semantic-action-definitions-v1"
        and companion.get("format_version") == 1
        and companion.get("definition_count") == 5
        and companion.get("action_count") == 8
        and companion.get("semantic_binding_artifact_sha256") == mapping_sha
        and report.get("semantic_action_definitions_sha256")
            == sha256(action_definitions)
        and signed_semantic.get("action_definitions_sha256")
            == sha256(action_definitions),
        "signed semantic action-definition companion differs from cfg5401",
    )
    bindings = semantic.get("bindings")
    require(isinstance(bindings, list) and len(bindings) == 56
            and semantic.get("binding_count") == 56,
            "semantic binding catalog is not the expected 56-entry catalog")
    binding_by_id = {
        binding.get("semantic_binding_id"): binding for binding in bindings
    }
    require(len(binding_by_id) == 56 and None not in binding_by_id,
            "semantic binding IDs are not unique")

    plans = report.get("semantic_action_plans")
    require(isinstance(plans, list) and len(plans) == 8,
            "cfg5401 action plan set is incomplete")
    sv_plans = [
        plan for plan in plans
        if str(plan.get("action_definition_id", "")).startswith(SV_ACTION_PREFIX)
    ]
    require(len(sv_plans) == 6
            and all(plan.get("enabled") is False
                    and plan.get("qualification") == "unqualified"
                    and plan.get("disabled_reason")
                    == "reference_unit_to_rpm_conversion_not_bound"
                    for plan in sv_plans),
            "an SV630N action is unexpectedly qualified or enabled")
    set_bindings, channels = validate_xb6_action(
        package_action(plans, XB6_SET_ACTION), binding_by_id, True
    )
    clear_bindings, _ = validate_xb6_action(
        package_action(plans, XB6_CLEAR_ACTION), binding_by_id, False
    )
    require(
        {as_int(binding["resource_id"]) for binding in set_bindings}
        == {as_int(binding["resource_id"]) for binding in clear_bindings},
        "XB6 set and clear actions do not cover the same output group",
    )
    policies = [
        policy for policy in report.get("output_transaction_policies", [])
        if as_int(policy.get("consistency_group_id")) == EXPECTED_XB6_GROUP
    ]
    require(len(policies) == 1
            and policies[0].get("recovery_policy") == "hold_safe"
            and policies[0].get("max_ttl_cycles") == 1000,
            "XB6 output policy is not exact HOLD_SAFE/1000")
    topology = manifest.get("topology")
    require(isinstance(topology, list) and len(topology) == 3,
            "signed package topology has the wrong slave count")
    signed_identity = tuple(
        (entry.get("position"), entry.get("station_address"),
         entry.get("vendor_id"), entry.get("product_code"),
         entry.get("revision"))
        for entry in topology
    )
    require(
        signed_identity == tuple(entry[:5] for entry in EXPECTED_IDENTITIES),
        "signed package topology does not match the expected XB6/SV630N chain",
    )
    return {
        "package_path": package_path,
        "package_bytes": package_bytes,
        "package_sha": package_sha,
        "manifest": manifest,
        "manifest_sha": sha256(manifest_bytes),
        "mapping": bytes.fromhex(mapping_sha),
        "mapping_sha": mapping_sha,
        "semantic": semantic,
        "output_bindings": set_bindings,
        "channel_by_resource": channels,
        "group": EXPECTED_XB6_GROUP,
        "max_ttl": policies[0]["max_ttl_cycles"],
    }


def checked_file(path: Path, expected_sha: str, label: str) -> None:
    require(path.is_file(), f"{label} is missing: {path}")
    require(sha256(path.read_bytes()) == expected_sha,
            f"{label} SHA-256 mismatch")


def load_reference_client(args: argparse.Namespace) -> types.ModuleType:
    client_path = args.reference_client.resolve()
    support_path = args.reference_support.resolve()
    capacity_module = support_path / "product_capacity_contract.py"
    checked_file(client_path, EXPECTED_CLIENT_SHA, "v1.15 reference client")
    checked_file(capacity_module, EXPECTED_CAPACITY_MODULE_SHA,
                 "capacity contract module")
    checked_file(DEFAULT_CAPACITY_CONTRACT, EXPECTED_CAPACITY_CONTRACT_SHA,
                 "capacity contract")
    require(str(support_path) not in sys.path,
            "reference support directory was already imported from")
    sys.path.insert(0, str(support_path))
    name = "_api054_cfg5401_v15_reference_client"
    spec = importlib.util.spec_from_file_location(name, client_path)
    require(spec is not None and spec.loader is not None,
            "cannot construct the v1.15 reference-client loader")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    require(module.PROTOCOL_MINOR == 15,
            "reference client is not Product API v1.15")
    require(module.FEATURE_CURRENT_MASK == 0x0001FFFF,
            "reference client does not require the v1.15 feature set")
    return module


def selector_tuple(selector: Any) -> tuple[str, int, int] | None:
    if selector is None:
        return None
    return selector.slot, selector.generation, selector.configuration_id


def assert_hello(client: Any, api: types.ModuleType, expected_boot: int) -> None:
    require(client.protocol_minor == 15,
            "controller did not negotiate Product API v1.15")
    require(client.boot_id == expected_boot, "controller BootId changed")
    for role in (api.ROLE_CONTROL, api.ROLE_BULK):
        ack = client.hello_ack_by_role.get(role)
        require(ack is not None, f"missing HELLO_ACK for role {role}")
        require(ack.session_id == client.session_id and ack.boot_id == expected_boot,
                f"HELLO identity mismatch for role {role}")
        require(ack.feature_bits == 0x0001FFFF,
                f"controller feature set mismatch for role {role}")


def assert_no_lease(client: Any, api: types.ModuleType) -> None:
    for role in (api.ROLE_CONTROL, api.ROLE_BULK):
        require(client.hello_ack_by_role[role].lease_owner_session_id == 0,
                f"controller already has a control owner on role {role}")


def assert_shutdown(state: Any, api: types.ModuleType) -> None:
    require(state.service_state == api.CONTROLLER_SERVICE_SHUTDOWN,
            "controller is not in SHUTDOWN")
    require(state.status == api.CONTROLLER_STATUS_READY,
            "SHUTDOWN status is not exactly READY")
    require(state.current_faults == 0 and state.latched_faults == 0,
            "controller has a current or latched fault")
    require(state.al_state == 0 and state.expected_wkc == 0
            and state.last_wkc == 0 and state.safe_output == 0,
            "SHUTDOWN state has active AL/WKC/output fields")


def assert_runtime_empty(package: Any, api: types.ModuleType) -> None:
    require(package.cpu1_package_state == api.PACKAGE_CPU1_STATE_EMPTY
            and package.cpu1_result == 0
            and package.cpu1_request_sequence == 0
            and package.cpu1_boot_id == api.PACKAGE_CPU1_BOOT_ID_UNAVAILABLE,
            "CPU1 runtime package is not in the exact EMPTY state")


def same_shutdown_baseline(first: Any, second: Any) -> bool:
    """Compare only fields that must not vary in an idle management loop."""
    fields = (
        "service_state", "status", "current_faults", "latched_faults",
        "boot_id", "al_state", "expected_wkc", "last_wkc", "safe_output",
        "last_alarm_sequence",
    )
    return all(getattr(first, field) == getattr(second, field) for field in fields)


def assert_op_safe(state: Any, api: types.ModuleType) -> None:
    require(state.service_state == api.CONTROLLER_SERVICE_OP_SAFE,
            "controller is not OP_SAFE")
    require(state.current_faults == 0 and state.latched_faults == 0,
            "OP_SAFE state has a fault")
    required = (api.CONTROLLER_STATUS_READY | api.CONTROLLER_STATUS_BUS_OP
                | api.CONTROLLER_STATUS_SAFE_OUTPUT
                | api.CONTROLLER_STATUS_DC_LOCKED)
    require(state.status & required == required,
            "OP_SAFE lacks READY/BUS_OP/SAFE_OUTPUT/DC_LOCKED")
    require(state.al_state == api.CONTROLLER_AL_OP
            and state.expected_wkc == 11 and state.last_wkc == 11,
            "OP_SAFE does not have AL=OP and WKC=11/11")


def assert_running(state: Any, api: types.ModuleType) -> None:
    require(state.service_state == api.CONTROLLER_SERVICE_RUNNING,
            "controller did not enter RUNNING")
    require(state.current_faults == 0 and state.latched_faults == 0,
            "RUNNING state has a fault")
    required = (api.CONTROLLER_STATUS_READY | api.CONTROLLER_STATUS_BUS_OP
                | api.CONTROLLER_STATUS_ACTIVE | api.CONTROLLER_STATUS_DC_LOCKED)
    require(state.status & required == required,
            "RUNNING lacks READY/BUS_OP/ACTIVE/DC_LOCKED")
    require(state.al_state == api.CONTROLLER_AL_OP
            and state.expected_wkc == 11 and state.last_wkc == 11,
            "RUNNING does not have AL=OP and WKC=11/11")
    require(state.dc_abs_diff_ns < 125000,
            "RUNNING DC difference exceeds a configured cycle")


def assert_topology(evidence: Any, api: types.ModuleType) -> None:
    require(len(evidence.slaves) == 3 and len(evidence.modules) == 1,
            "real topology evidence does not contain the expected 3+1 records")
    actual = tuple(
        (record["position"], record["station_address"], record["vendor_id"],
         record["product_code"], record["revision"], record["alias"])
        for record in evidence.slaves
    )
    require(actual == EXPECTED_IDENTITIES,
            "real topology identities or aliases differ from cfg5401 evidence")
    first, second, third = evidence.slaves
    require(first["module_validity"] == api.EVIDENCE_VALID
            and first["module_count"] == 1
            and second["module_validity"] == api.EVIDENCE_UNAVAILABLE
            and second["module_count"] == 0
            and third["module_validity"] == api.EVIDENCE_UNAVAILABLE
            and third["module_count"] == 0,
            "real module evidence differs from the XB6/SV630N contract")
    module = evidence.modules[0]
    require(module == {
        "parent_position": 0, "slot": 1, "module_ident": 1572,
        "validity": api.EVIDENCE_VALID,
        "provenance": api.EVIDENCE_PROVENANCE_DEVICE_REPORTED,
        "source": api.EVIDENCE_SOURCE_COE_DETECTED_MODULES,
    }, "real XB6 module evidence does not match the signed DO16 module")


def read_all_resources(client: Any, slot: str) -> tuple[Any, list[Any]]:
    first = client.query_resource_table(slot, 64)
    binding = first.binding
    resources = list(first.resources)
    cursor = first.next_cursor
    while cursor is not None:
        page = client.query_resource_table(slot, 64, cursor, binding)
        require(page.binding == binding, "resource table binding changed mid-page")
        resources.extend(page.resources)
        cursor = page.next_cursor
    ids = [resource.resource_id for resource in resources]
    require(len(resources) == first.total_resources == 56
            and ids == sorted(ids) and len(ids) == len(set(ids)),
            "runtime resource table is not the exact sorted 56-entry catalog")
    return binding, resources


def verify_runtime(
    client: Any, api: types.ModuleType, offline: dict[str, Any],
    selector: Any, evidence: Evidence,
) -> tuple[Any, dict[int, Any], Any]:
    binding, resources = read_all_resources(client, selector.slot)
    require(binding.slot == selector.slot
            and binding.generation == selector.generation
            and binding.configuration_id == EXPECTED_CONFIG_ID
            and binding.catalog_revision == EXPECTED_CATALOG_REVISION
            and binding.topology_identity == EXPECTED_TOPOLOGY_IDENTITY
            and binding.boot_id == client.boot_id,
            "runtime resource epoch does not match the activated cfg5401")
    runtime_by_id = {resource.resource_id: resource for resource in resources}
    signed = offline["semantic"]["bindings"]
    signed_ids = {as_int(value["resource_id"]) for value in signed}
    require(set(runtime_by_id) == signed_ids,
            "runtime ResourceIds do not equal the signed semantic catalog")
    primitive_codes = {
        "bool": 1, "u8": 2, "s8": 3, "u16": 4, "s16": 5,
        "u32": 6, "s32": 7, "u64": 8, "s64": 9, "q32_32": 10,
        "raw_bits": 11,
    }
    direction_codes = {"input": 1, "output": 2}
    access_codes = {"read_only": 1, "read_write": 3}
    for declaration in signed:
        resource = runtime_by_id[as_int(declaration["resource_id"])]
        safe_value = (
            int(declaration["safe_value"]).to_bytes(
                (declaration["bit_width"] + 7) // 8, "big"
            ) if declaration["safe_value_declared"] else None
        )
        require(
            resource.component_instance_id
                == as_int(declaration["component_instance_id"])
            and resource.consistency_group_id
                == as_int(declaration["consistency_group_id"])
            and resource.data_type == primitive_codes[declaration["primitive"]]
            and resource.bit_width == declaration["bit_width"]
            and resource.process_image_bit_length == declaration["bit_width"]
            and resource.direction == direction_codes[declaration["direction"]]
            and resource.access == access_codes[declaration["access"]]
            and resource.safe_value == safe_value,
            f"runtime resource {resource.resource_id:#x} differs from signed data",
        )
    for signed_output in offline["output_bindings"]:
        resource_id = as_int(signed_output["resource_id"])
        resource = runtime_by_id.get(resource_id)
        require(resource is not None
                and resource.data_type == 1 and resource.bit_width == 1
                and resource.direction == 2 and resource.access == 3
                and resource.safe_value == b"\x00"
                and resource.consistency_group_id == offline["group"],
                "a signed XB6 writable output is absent or unsafe at runtime")

    attestation = client.get_semantic_binding_attestation(binding)
    expected_security = (api.SEMANTIC_ATTEST_SECURITY_SIGNED
                         | api.SEMANTIC_ATTEST_SECURITY_VERIFIED
                         | api.SEMANTIC_ATTEST_SECURITY_PRODUCTION
                         | api.SEMANTIC_ATTEST_SECURITY_BINDING)
    signed_metadata = offline["manifest"]["semantic_binding"]
    require(
        attestation.binding == binding and attestation.format_version == 2
        and attestation.security_flags == expected_security
        and attestation.binding_count == 56
        and attestation.package_sha256 == bytes.fromhex(offline["package_sha"])
        and attestation.manifest_sha256
            == bytes.fromhex(offline["manifest_sha"])
        and attestation.mapping_sha256 == offline["mapping"]
        and attestation.resource_records_sha256
            == bytes.fromhex(signed_metadata["resource_records_sha256"])
        and attestation.resource_section_sha256
            == bytes.fromhex(signed_metadata["resource_section_sha256"])
        and attestation.topology_sha256
            == bytes.fromhex(signed_metadata["topology_sha256"])
        and attestation.key_id == bytes.fromhex(EXPECTED_KEY_ID),
        "controller attestation does not match the exact signed ECPKG",
    )
    policy = client.query_output_group_policy(
        binding, offline["mapping"], offline["group"]
    )
    require(
        policy.binding == binding and policy.consistency_group_id == offline["group"]
        and policy.recovery_policy == api.OUTPUT_RECOVERY_HOLD_SAFE
        and policy.max_ttl_cycles == offline["max_ttl"] == 1000
        and policy.resource_count == 16 and policy.mapping_sha256 == offline["mapping"],
        "controller XB6 output policy does not match the signed policy",
    )
    state = client.get_output_transaction_state(binding, offline["mapping"])
    require(state.state == api.OUTPUT_STATE_IDLE
            and state.output_generation == policy.output_generation,
            "output transaction state is not initial IDLE/current generation")
    evidence.add("runtime-attestation-and-policy", "pass", binding=binding,
                 resource_count=len(resources), attestation=attestation,
                 policy=policy, output_state=state)
    return binding, runtime_by_id, policy


def output_values(api: types.ModuleType, offline: dict[str, Any], do0: int) -> tuple[Any, ...]:
    values = []
    for declaration in offline["output_bindings"]:
        resource_id = as_int(declaration["resource_id"])
        channel = offline["channel_by_resource"][resource_id]
        values.append(api.OutputValue(
            resource_id, 1, 1, bytes([do0 if channel == 0 else 0])
        ))
    return tuple(sorted(values, key=lambda value: value.resource_id))


def snapshot_values(snapshot: Any) -> dict[int, int]:
    result: dict[int, int] = {}
    for value in snapshot.values:
        require(value.quality == 0x03 and len(value.value) == 1,
                "output snapshot is not VALID|FRESH one-byte data")
        result[value.resource_id] = value.value[0]
    require(len(result) == len(snapshot.values),
            "output snapshot contains duplicate resource IDs")
    return result


def safe_cleanup(
    client: Any | None, api: types.ModuleType, evidence: Evidence,
    leased: bool,
) -> None:
    if client is None:
        return
    try:
        state = client.get_state()
        if leased and state.service_state in {
            api.CONTROLLER_SERVICE_RUNNING, api.CONTROLLER_SERVICE_PAUSED,
        }:
            client.controlled_stop()
            evidence.add("exception-controlled-stop", "pass",
                         state=client.get_state())
            state = client.get_state()
        if leased and state.service_state == api.CONTROLLER_SERVICE_OP_SAFE:
            client.enter_configuration_mode()
            evidence.add("exception-enter-configuration", "pass",
                         state=client.get_state())
    except Exception as error:
        evidence.add("exception-safe-cleanup", "error", error=str(error))
    finally:
        if leased:
            try:
                client.release_control()
                evidence.add("exception-release", "pass")
            except Exception as error:
                evidence.add("exception-release", "error", error=str(error))
        client.close()


def execute_hardware(
    args: argparse.Namespace, api: types.ModuleType, offline: dict[str, Any],
    evidence: Evidence,
) -> None:
    client: Any | None = None
    leased = False
    completed = False
    active_selector: Any | None = None
    try:
        client = api.ProductApiClient(args.host, args.base_port, args.timeout)
        client.connect()
        assert_hello(client, api, args.expected_boot_id)
        assert_no_lease(client, api)
        state_one = client.get_state()
        package_one = client.get_package_state()
        time.sleep(args.sample_interval)
        state_two = client.get_state()
        package_two = client.get_package_state()
        assert_shutdown(state_one, api)
        assert_shutdown(state_two, api)
        assert_runtime_empty(package_one, api)
        assert_runtime_empty(package_two, api)
        require(
            same_shutdown_baseline(state_one, state_two)
            and selector_tuple(package_one.active) == selector_tuple(package_two.active)
            and selector_tuple(package_one.staged) == selector_tuple(package_two.staged),
            "two read-only baseline snapshots are inconsistent",
        )
        evidence.add("baseline-read-only", "pass", state=state_two,
                     package_state=package_two)

        client.acquire_control(30000)
        leased = True
        client.enter_configuration_mode()
        topology = client.discover_topology_evidence()
        assert_topology(topology, api)
        evidence.add("real-topology-evidence", "pass", topology=topology)

        client.heartbeat()
        staged = client.upload_ecpkg(
            offline["package_bytes"], EXPECTED_CONFIG_ID
        )
        require(staged.configuration_id == EXPECTED_CONFIG_ID,
                "ECPKG upload returned the wrong ConfigurationId")
        evidence.add("upload-signed-ecpkg", "pass", staged=staged,
                     package_sha=offline["package_sha"])
        client.heartbeat()
        validated = client.validate_package(staged)
        require(
            selector_tuple(validated.staged) == selector_tuple(staged)
            and validated.cpu1_package_state == 2
            and validated.cpu1_result == 0
            and validated.cpu1_boot_id == args.expected_boot_id,
            "controller did not accept the staged cfg5401 package",
        )
        evidence.add("validate-signed-ecpkg", "pass", package_state=validated)
        client.heartbeat()
        activated = client.activate_package(staged)
        require(
            selector_tuple(activated.active) == selector_tuple(staged)
            and activated.cpu1_package_state == 3
            and activated.cpu1_result == 0
            and activated.cpu1_boot_id == args.expected_boot_id,
            "controller did not activate the staged cfg5401 package",
        )
        active_selector = activated.active
        require(active_selector is not None, "activation has no active selector")
        op_safe = client.get_state()
        assert_op_safe(op_safe, api)
        evidence.add("activate-signed-ecpkg", "pass", package_state=activated,
                     controller_state=op_safe)

        binding, _, policy = verify_runtime(
            client, api, offline, active_selector, evidence
        )
        client.heartbeat()
        client.start_dc()
        first_state, first_performance = client.get_performance_snapshot()
        time.sleep(args.sample_interval)
        second_state, second_performance = client.get_performance_snapshot()
        assert_running(first_state, api)
        assert_running(second_state, api)
        require(second_state.cycle_count > first_state.cycle_count,
                "DC cycle count did not advance")
        evidence.add("dc-running", "pass", first_state=first_state,
                     second_state=second_state,
                     first_performance=first_performance,
                     second_performance=second_performance)

        values_one = output_values(api, offline, 1)
        output_ids = tuple(value.resource_id for value in values_one)
        operation_one = secrets.token_bytes(16)
        client.heartbeat()
        applied = client.apply_output_transaction(
            binding, offline["mapping"], operation_one, policy.output_generation,
            policy.max_ttl_cycles, offline["group"], values_one,
        )
        require(applied.output_generation == policy.output_generation + 1
                and applied.value_count == 16
                and applied.expiry_cycle == applied.applied_cycle + policy.max_ttl_cycles,
                "XB6 DO0 transaction did not return an exact applied result")
        evidence.add("xb6-do0-transaction", "pass", operation_id=operation_one,
                     result=applied)
        # Persist acceptance before querying state so a later observation error
        # does not erase whether the controller accepted the full signed group.
        evidence.write()
        observed_state = client.get_output_transaction_state(binding, offline["mapping"])
        first_snapshot = client.get_resource_snapshot(binding, output_ids)
        expected_one = {
            resource_id: int(offline["channel_by_resource"][resource_id] == 0)
            for resource_id in output_ids
        }
        observed_values = snapshot_values(first_snapshot)
        if first_snapshot.capture_cycle < applied.expiry_cycle:
            require(observed_state.state == api.OUTPUT_STATE_OVERRIDE_ACTIVE
                    and observed_values == expected_one,
                    "XB6 DO0 override was not visible before its TTL expired")
            observation = "override-visible"
        else:
            require(observed_values == {resource_id: 0 for resource_id in output_ids},
                    "expired XB6 transaction did not return its outputs safe")
            observation = "ttl-safe-before-snapshot"
        evidence.add("xb6-do0-observation", "pass", observation=observation,
                     output_state=observed_state, snapshot=first_snapshot)

        clear_base = client.get_output_transaction_state(binding, offline["mapping"])
        values_zero = output_values(api, offline, 0)
        operation_zero = secrets.token_bytes(16)
        cleared = client.apply_output_transaction(
            binding, offline["mapping"], operation_zero,
            clear_base.output_generation, policy.max_ttl_cycles,
            offline["group"], values_zero,
        )
        cleared_snapshot = client.get_resource_snapshot(binding, output_ids)
        require(snapshot_values(cleared_snapshot)
                == {resource_id: 0 for resource_id in output_ids},
                "explicit XB6 all-zero transaction did not produce safe outputs")
        evidence.add("xb6-explicit-safe-transaction", "pass",
                     operation_id=operation_zero, result=cleared,
                     snapshot=cleared_snapshot)

        client.controlled_stop()
        stopped = client.get_state()
        assert_op_safe(stopped, api)
        client.enter_configuration_mode()
        shutdown = client.get_state()
        shutdown_package = client.get_package_state()
        assert_shutdown(shutdown, api)
        assert_runtime_empty(shutdown_package, api)
        require(shutdown_package.active is not None
                and shutdown_package.active.configuration_id == EXPECTED_CONFIG_ID,
                "cleanup changed the persistent cfg5401 selector")
        client.release_control()
        leased = False
        client.close()
        client = None
        evidence.add("nominal-cleanup", "pass", stopped=stopped,
                     shutdown=shutdown, package_state=shutdown_package)

        final = api.ProductApiClient(args.host, args.base_port, args.timeout)
        try:
            final.connect()
            assert_hello(final, api, args.expected_boot_id)
            assert_no_lease(final, api)
            final_state = final.get_state()
            final_package = final.get_package_state()
            assert_shutdown(final_state, api)
            assert_runtime_empty(final_package, api)
            require(final_package.active is not None
                    and final_package.active.configuration_id == EXPECTED_CONFIG_ID,
                    "final persistent selector is not cfg5401")
            evidence.add("final-read-only", "pass", state=final_state,
                         package_state=final_package)
        finally:
            final.close()
        completed = True
    finally:
        if not completed:
            safe_cleanup(client, api, evidence, leased)


def plan(args: argparse.Namespace, offline: dict[str, Any]) -> dict[str, Any]:
    return {
        "network_access": bool(args.execute),
        "controller": {"host": args.host, "base_port": args.base_port},
        "package": {
            "path": offline["package_path"], "sha256": offline["package_sha"],
            "configuration_id": EXPECTED_CONFIG_ID,
            "mapping_sha256": offline["mapping_sha"],
            "dc_cycle_ns": 125000, "expected_wkc": 11,
        },
        "steps": [
            "verify ECPKG v2/signature/action contracts locally",
            "read-only v1.15 SHUTDOWN and lease-free baseline",
            "AcquireControl, topology evidence scan, upload/validate/activate",
            "verify runtime resources, semantic attestation and XB6 policy",
            "StartDC and verify running WKC/DC/cycle progression",
            "apply signed complete XB6 DO0=1 group, then explicit all-zero group",
            "ControlledStop, configuration shutdown, release and final read-only check",
        ],
        "forbidden": [
            "SV630N action or motion request", "FreeRun", "ResetFault",
            "NetworkQuickStop", "raw PDO/SDO/register access",
            "Embed Labs Local Bridge/plugin", "visible GUI",
        ],
    }


def default_evidence(mode: str) -> Path:
    timestamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return ROOT / "build/logs" / f"api054_cfg5401_{mode}_{timestamp}.json"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Fail-closed API-054 cfg5401 Product API v1.15 validation"
    )
    parser.add_argument("--execute", action="store_true",
                        help="permit headless controller traffic")
    parser.add_argument("--deploy", action="store_true",
                        help="permit ECPKG upload, activation, DC and XB6 test")
    parser.add_argument("--confirmation")
    parser.add_argument("--host", default="192.168.3.101")
    parser.add_argument("--base-port", type=int, default=15200)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--sample-interval", type=float, default=0.10)
    parser.add_argument("--expected-boot-id", type=parse_int)
    parser.add_argument("--package", type=Path, default=DEFAULT_PACKAGE)
    parser.add_argument("--public-key", type=Path, default=DEFAULT_PUBLIC_KEY)
    parser.add_argument("--reference-client", type=Path,
                        default=DEFAULT_REFERENCE_CLIENT)
    parser.add_argument("--reference-support", type=Path,
                        default=DEFAULT_REFERENCE_SUPPORT)
    parser.add_argument("--expected-package-sha", default=EXPECTED_PACKAGE_SHA)
    parser.add_argument("--expected-mapping-sha", default=EXPECTED_MAPPING_SHA)
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args(argv)
    args.expected_package_sha = args.expected_package_sha.lower()
    args.expected_mapping_sha = args.expected_mapping_sha.lower()
    require(len(args.expected_package_sha) == 64,
            "expected package SHA must contain 64 hex characters")
    require(len(args.expected_mapping_sha) == 64,
            "expected mapping SHA must contain 64 hex characters")
    require(args.timeout > 0 and args.sample_interval > 0,
            "timeout and sample interval must be positive")
    if args.execute:
        require(args.deploy, "--execute requires --deploy")
        require(args.confirmation == EXECUTION_CONFIRMATION,
                "--execute requires the exact API054 confirmation string")
        require(args.expected_boot_id is not None and args.expected_boot_id > 0,
                "--execute requires a nonzero --expected-boot-id")
    else:
        require(not args.deploy, "--deploy requires --execute")
    return args


def main(argv: list[str]) -> int:
    evidence: Evidence | None = None
    try:
        args = parse_args(argv)
        mode = "execute" if args.execute else "offline"
        evidence = Evidence((args.evidence or default_evidence(mode)).resolve(), mode)
        evidence.document["arguments"] = jsonable(vars(args))
        offline = load_package(args)
        evidence.add("offline-signed-package", "pass",
                     package_sha=offline["package_sha"],
                     manifest_sha=offline["manifest_sha"],
                     mapping_sha=offline["mapping_sha"],
                     xb6_group=f"0x{offline['group']:08x}")
        api = load_reference_client(args)
        evidence.add("reference-client", "pass", protocol_minor=api.PROTOCOL_MINOR,
                     feature_mask=f"0x{api.FEATURE_CURRENT_MASK:08x}")
        execution_plan = plan(args, offline)
        if not args.execute:
            evidence.add("offline-plan", "pass", plan=execution_plan)
            evidence.finish("pass", plan=execution_plan)
            evidence.write()
            print(json.dumps(jsonable(execution_plan), indent=2, sort_keys=True))
            print(f"evidence: {evidence.path}")
            return 0
        execute_hardware(args, api, offline, evidence)
        evidence.finish("pass", plan=execution_plan)
        evidence.write()
        print(f"hardware acceptance PASS; evidence: {evidence.path}")
        return 0
    except Exception as error:
        if evidence is not None:
            evidence.add("terminal", "fail", error_type=type(error).__name__,
                         error=str(error))
            evidence.finish("fail")
            evidence.write()
            print(f"evidence: {evidence.path}", file=sys.stderr)
        print(f"FAIL: {type(error).__name__}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
