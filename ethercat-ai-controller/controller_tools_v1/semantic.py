"""Cross-document and safety validation for controller-tools-v1 artifacts."""

from __future__ import annotations

import hashlib
import json
import math
from collections import defaultdict
from typing import Any

from .errors import ErrorCode
from .repository import MockRepository, identity_in_scope, identity_key
from .schema import ValidationIssue


def _issue(code: ErrorCode | str, path: str, message: str) -> ValidationIssue:
    return ValidationIssue(
        code.value if isinstance(code, ErrorCode) else code, path, message
    )


def _sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _adapter_identity_revision_keys(
    adapter: dict[str, Any],
) -> list[tuple[int, int, int]]:
    scope = adapter["spec"]["identityScope"]
    lower = int(scope["minimumRevision"])
    upper = int(scope["maximumRevision"])
    if upper - lower > 4096:
        return []
    return [
        (int(scope["vendorId"]), int(scope["productCode"]), revision)
        for revision in range(lower, upper + 1)
    ]


class ContractValidator:
    """Combines structural schema checks with deterministic domain gates."""

    def __init__(self, repository: MockRepository):
        self.repository = repository
        self.schemas = repository.schema_registry

    def validate(self, document: Any) -> list[ValidationIssue]:
        structural = self.schemas.validate(document)
        if structural:
            return structural
        kind = document["kind"]
        if kind == "ControlIntent":
            return self._validate_control_intent(document)
        if kind == "NormalizedDeviceModel":
            return self._validate_device_model(document)
        if kind == "AdapterManifest":
            return self._validate_adapter(document)
        if kind == "ControllerProject":
            return self._validate_project(document)
        return [
            _issue(
                ErrorCode.FORMAT_INVALID,
                "$.kind",
                f"Unsupported artifact kind {kind!r}.",
            )
        ]

    def validate_repository(self) -> list[ValidationIssue]:
        issues: list[ValidationIssue] = []
        for device in self.repository.devices.values():
            issues.extend(self.validate(device))
        for adapter in self.repository.adapters.values():
            issues.extend(self.validate(adapter))
        issues.extend(self.validate(self.repository.project))
        issues.extend(self.validate(self.repository.intent))
        return issues

    def _validate_device_model(
        self, document: dict[str, Any]
    ) -> list[ValidationIssue]:
        issues: list[ValidationIssue] = []
        spec = document["spec"]
        cycle = spec["cycle"]
        if cycle["minimumUs"] > cycle["maximumUs"]:
            issues.append(
                _issue(
                    ErrorCode.FORMAT_INVALID,
                    "$.spec.cycle",
                    "minimumUs must not exceed maximumUs.",
                )
            )
        for index, value in enumerate(cycle["supportedUs"]):
            if not cycle["minimumUs"] <= value <= cycle["maximumUs"]:
                issues.append(
                    _issue(
                        ErrorCode.FORMAT_INVALID,
                        f"$.spec.cycle.supportedUs[{index}]",
                        "Supported cycle lies outside the declared cycle range.",
                    )
                )

        process_image = spec["processImage"]
        for index, variable in enumerate(process_image["variables"]):
            size = process_image[
                "inputBytes" if variable["direction"] == "input" else "outputBytes"
            ]
            end_bit = (
                int(variable["byteOffset"]) * 8
                + int(variable["bitOffset"])
                + int(variable["bitLength"])
            )
            if end_bit > int(size) * 8:
                issues.append(
                    _issue(
                        ErrorCode.PDO_CAPACITY_EXCEEDED,
                        f"$.spec.processImage.variables[{index}]",
                        f"Variable ends at bit {end_bit}, beyond {size} bytes.",
                    )
                )

        for direction, pdos_name, bytes_name in (
            ("output", "rxPdos", "outputBytes"),
            ("input", "txPdos", "inputBytes"),
        ):
            total_bits = sum(
                int(entry["bitLength"])
                for pdo in spec[pdos_name]
                for entry in pdo["entries"]
            )
            capacity_bits = int(process_image[bytes_name]) * 8
            if total_bits > capacity_bits:
                issues.append(
                    _issue(
                        ErrorCode.PDO_CAPACITY_EXCEEDED,
                        f"$.spec.{pdos_name}",
                        f"{direction} PDOs require {total_bits} bits but the model "
                        f"declares {capacity_bits} bits.",
                    )
                )

        private = spec["privateInitialization"]
        if private["status"] == "missing-information" and not private["missingInformation"]:
            issues.append(
                _issue(
                    ErrorCode.FORMAT_INVALID,
                    "$.spec.privateInitialization.missingInformation",
                    "Missing vendor initialization must name the unavailable information.",
                )
            )
        if private["status"] != "documented" and private["steps"]:
            issues.append(
                _issue(
                    ErrorCode.FORMAT_INVALID,
                    "$.spec.privateInitialization.steps",
                    "Private steps are allowed only when their status is documented.",
                )
            )
        return issues

    def _validate_adapter(self, document: dict[str, Any]) -> list[ValidationIssue]:
        issues: list[ValidationIssue] = []
        spec = document["spec"]
        scope = spec["identityScope"]
        if scope["minimumRevision"] > scope["maximumRevision"]:
            issues.append(
                _issue(
                    ErrorCode.FORMAT_INVALID,
                    "$.spec.identityScope",
                    "minimumRevision must not exceed maximumRevision.",
                )
            )

        model_ref = spec["deviceModel"]
        model = self.repository.devices.get(model_ref["id"])
        if model is None:
            issues.append(
                _issue(
                    ErrorCode.REFERENCE_NOT_FOUND,
                    "$.spec.deviceModel.id",
                    f"Device model {model_ref['id']!r} is not registered.",
                )
            )
        else:
            if model["metadata"]["version"] != model_ref["version"]:
                issues.append(
                    _issue(
                        ErrorCode.REFERENCE_NOT_FOUND,
                        "$.spec.deviceModel.version",
                        "Adapter references a device-model version that is not registered.",
                    )
                )
            actual_hash = self.repository.device_file_sha256.get(model_ref["id"])
            if actual_hash and actual_hash != model_ref["sha256"]:
                issues.append(
                    _issue(
                        ErrorCode.REFERENCE_NOT_FOUND,
                        "$.spec.deviceModel.sha256",
                        "Device-model content hash does not match the registered file.",
                    )
                )
            identity = model["spec"]["identity"]
            if not identity_in_scope(identity, scope):
                issues.append(
                    _issue(
                        ErrorCode.UNKNOWN_DEVICE,
                        "$.spec.identityScope",
                        "Adapter identity scope does not include its normalized model.",
                    )
                )
            esi_hash = model["spec"]["esiSource"]["sha256"]
            if esi_hash not in spec["esiSha256"]:
                issues.append(
                    _issue(
                        ErrorCode.ESI_CONFLICT,
                        "$.spec.esiSha256",
                        "Adapter does not include the normalized model's exact ESI hash.",
                    )
                )

        state = spec["validationState"]
        if state == "verified":
            passed_kinds = {
                evidence["kind"]
                for evidence in spec["evidence"]
                if evidence["result"] == "passed"
            }
            if spec["signature"] is None or not {
                "schema-validation",
                "engineering-review",
            }.issubset(passed_kinds):
                issues.append(
                    _issue(
                        ErrorCode.ADAPTER_UNVERIFIED,
                        "$.spec",
                        "A verified adapter requires a signature plus passed schema "
                        "validation and engineering review evidence.",
                    )
                )
        elif spec["realHardwareAllowed"]:
            issues.append(
                _issue(
                    ErrorCode.ADAPTER_UNVERIFIED,
                    "$.spec.realHardwareAllowed",
                    "Candidate, mock-only, and revoked adapters cannot allow real hardware.",
                )
            )
        return issues

    def _registry_conflicts(self) -> list[ValidationIssue]:
        by_identity: dict[tuple[int, int, int], list[dict[str, Any]]] = defaultdict(
            list
        )
        broad_scopes: list[dict[str, Any]] = []
        for adapter in self.repository.adapters.values():
            keys = _adapter_identity_revision_keys(adapter)
            if keys:
                for key in keys:
                    by_identity[key].append(adapter)
            else:
                broad_scopes.append(adapter)

        issues: list[ValidationIssue] = []
        for key, adapters in sorted(by_identity.items()):
            if len(adapters) < 2:
                continue
            hash_sets = {tuple(sorted(item["spec"]["esiSha256"])) for item in adapters}
            top_priority = max(int(item["spec"]["matchPriority"]) for item in adapters)
            top = [
                item
                for item in adapters
                if int(item["spec"]["matchPriority"]) == top_priority
            ]
            if len(hash_sets) > 1 or len(top) > 1:
                ids = ", ".join(item["metadata"]["id"] for item in adapters)
                issues.append(
                    _issue(
                        ErrorCode.ESI_CONFLICT,
                        "$.spec.topology.slaves",
                        f"Identity {key} has overlapping adapters with ambiguous ESI "
                        f"scope: {ids}.",
                    )
                )
        if broad_scopes:
            issues.append(
                _issue(
                    ErrorCode.ESI_CONFLICT,
                    "$.spec.topology.slaves",
                    "Adapter revision range is too broad for deterministic conflict analysis.",
                )
            )
        return issues

    def _validate_project(self, document: dict[str, Any]) -> list[ValidationIssue]:
        issues = self._registry_conflicts()
        spec = document["spec"]
        controller_spec = self.repository.controller["spec"]
        controller_ref = spec["controller"]
        if controller_ref["controllerId"] != controller_spec["controllerId"]:
            issues.append(
                _issue(
                    ErrorCode.REFERENCE_NOT_FOUND,
                    "$.spec.controller.controllerId",
                    "Controller ID does not match the selected controller.",
                )
            )
        if controller_ref["bootId"] != controller_spec["bootId"]:
            issues.append(
                _issue(
                    ErrorCode.REFERENCE_NOT_FOUND,
                    "$.spec.controller.bootId",
                    "Boot ID is stale for the selected controller snapshot.",
                )
            )
        if controller_ref["capabilitySha256"] != controller_spec["capabilitySha256"]:
            issues.append(
                _issue(
                    ErrorCode.REFERENCE_NOT_FOUND,
                    "$.spec.controller.capabilitySha256",
                    "Capability fingerprint does not match the selected controller.",
                )
            )

        positions: set[int] = set()
        stations: set[int] = set()
        device_models_by_position: dict[int, dict[str, Any]] = {}
        for index, slave in enumerate(spec["topology"]["slaves"]):
            path = f"$.spec.topology.slaves[{index}]"
            if slave["position"] in positions:
                issues.append(
                    _issue(
                        ErrorCode.FORMAT_INVALID,
                        f"{path}.position",
                        "Slave position is duplicated.",
                    )
                )
            positions.add(slave["position"])
            if slave["stationAddress"] in stations:
                issues.append(
                    _issue(
                        ErrorCode.FORMAT_INVALID,
                        f"{path}.stationAddress",
                        "Station address is duplicated.",
                    )
                )
            stations.add(slave["stationAddress"])

            candidates = self.repository.adapter_candidates(
                slave["identity"], slave["esiSha256"]
            )
            if not candidates:
                identity_candidates = self.repository.adapter_candidates(slave["identity"])
                code = (
                    ErrorCode.ESI_CONFLICT
                    if len(
                        {
                            value
                            for candidate in identity_candidates
                            for value in candidate["spec"]["esiSha256"]
                        }
                    )
                    > 1
                    else ErrorCode.UNKNOWN_DEVICE
                )
                issues.append(
                    _issue(
                        code,
                        path,
                        "No deterministic adapter matches the exact identity and ESI hash.",
                    )
                )
                continue

            top_priority = int(candidates[0]["spec"]["matchPriority"])
            top = [
                candidate
                for candidate in candidates
                if int(candidate["spec"]["matchPriority"]) == top_priority
            ]
            if len(top) != 1:
                issues.append(
                    _issue(
                        ErrorCode.ESI_CONFLICT,
                        path,
                        "Multiple same-priority adapters match this exact ESI identity.",
                    )
                )
                continue
            adapter = top[0]
            adapter_ref = slave["adapter"]
            if adapter_ref["adapterId"] != adapter["metadata"]["id"]:
                issues.append(
                    _issue(
                        ErrorCode.REFERENCE_NOT_FOUND,
                        f"{path}.adapter.adapterId",
                        "Project does not reference the deterministic adapter winner.",
                    )
                )
            if adapter_ref["version"] != adapter["metadata"]["version"]:
                issues.append(
                    _issue(
                        ErrorCode.REFERENCE_NOT_FOUND,
                        f"{path}.adapter.version",
                        "Adapter version does not match the registry.",
                    )
                )
            actual_manifest_hash = self.repository.adapter_file_sha256.get(
                adapter["metadata"]["id"]
            )
            if actual_manifest_hash and (
                adapter_ref["manifestSha256"] != actual_manifest_hash
            ):
                issues.append(
                    _issue(
                        ErrorCode.REFERENCE_NOT_FOUND,
                        f"{path}.adapter.manifestSha256",
                        "Adapter manifest content hash does not match the registry.",
                    )
                )

            model = self.repository.devices.get(slave["deviceModelId"])
            if model is None:
                issues.append(
                    _issue(
                        ErrorCode.REFERENCE_NOT_FOUND,
                        f"{path}.deviceModelId",
                        "Referenced normalized device model is missing.",
                    )
                )
            else:
                device_models_by_position[slave["position"]] = model
                if identity_key(model["spec"]["identity"]) != identity_key(
                    slave["identity"]
                ):
                    issues.append(
                        _issue(
                            ErrorCode.UNKNOWN_DEVICE,
                            f"{path}.identity",
                            "Topology identity differs from the normalized device model.",
                        )
                    )
                if model["spec"]["esiSource"]["sha256"] != slave["esiSha256"]:
                    issues.append(
                        _issue(
                            ErrorCode.ESI_CONFLICT,
                            f"{path}.esiSha256",
                            "Topology ESI hash differs from the normalized model.",
                        )
                    )

            if (
                spec["topology"]["source"] != "mock"
                or spec["deployment"]["mode"] != "offline-only"
            ) and adapter["spec"]["validationState"] != "verified":
                issues.append(
                    _issue(
                        ErrorCode.ADAPTER_UNVERIFIED,
                        f"{path}.adapter",
                        "Only verified signed adapters may leave the mock/offline boundary.",
                    )
                )

        capability = controller_spec["capabilities"]
        timing = spec["timing"]
        minimum_cycle = int(capability["minimumCycleUs"])
        for model in device_models_by_position.values():
            minimum_cycle = max(minimum_cycle, int(model["spec"]["cycle"]["minimumUs"]))
            maximum_cycle = int(model["spec"]["cycle"]["maximumUs"])
            if timing["cycleUs"] > maximum_cycle:
                issues.append(
                    _issue(
                        ErrorCode.CYCLE_INFEASIBLE,
                        "$.spec.timing.cycleUs",
                        f"Cycle {timing['cycleUs']} us exceeds a device maximum "
                        f"of {maximum_cycle} us.",
                    )
                )
            supported = model["spec"]["cycle"]["supportedUs"]
            if supported and timing["cycleUs"] not in supported:
                issues.append(
                    _issue(
                        ErrorCode.CYCLE_INFEASIBLE,
                        "$.spec.timing.cycleUs",
                        f"Cycle {timing['cycleUs']} us is not listed by device "
                        f"{model['metadata']['id']}.",
                    )
                )
        if timing["cycleUs"] < minimum_cycle:
            issues.append(
                _issue(
                    ErrorCode.CYCLE_INFEASIBLE,
                    "$.spec.timing.cycleUs",
                    f"Cycle {timing['cycleUs']} us is below the required "
                    f"{minimum_cycle} us.",
                )
            )
        total_wcet_ns = sum(
            int(timing[field])
            for field in (
                "wireTimeNs",
                "masterWcetNs",
                "taskWcetNs",
                "safetyMarginNs",
            )
        )
        if total_wcet_ns > int(timing["cycleUs"]) * 1000:
            suggested = math.ceil(total_wcet_ns / 1000)
            issues.append(
                _issue(
                    ErrorCode.CYCLE_INFEASIBLE,
                    "$.spec.timing",
                    f"Budget is {total_wcet_ns} ns but the cycle is "
                    f"{timing['cycleUs'] * 1000} ns; suggested cycle is at least "
                    f"{suggested} us.",
                )
            )
        if timing["frameCount"] > capability["maximumCyclicFrames"]:
            issues.append(
                _issue(
                    ErrorCode.CYCLE_INFEASIBLE,
                    "$.spec.timing.frameCount",
                    "Cyclic frame count exceeds controller capability.",
                )
            )

        process_image = spec["processImage"]
        for direction in ("input", "output"):
            value = int(process_image[f"{direction}Bytes"])
            maximum = int(capability[f"maximumProcess{direction.title()}Bytes"])
            if value > maximum:
                issues.append(
                    _issue(
                        ErrorCode.PDO_CAPACITY_EXCEEDED,
                        f"$.spec.processImage.{direction}Bytes",
                        f"Project declares {value} {direction} bytes; controller "
                        f"maximum is {maximum}.",
                    )
                )

        for index, variable in enumerate(process_image["variables"]):
            path = f"$.spec.processImage.variables[{index}]"
            model = device_models_by_position.get(variable["slavePosition"])
            if model is None:
                continue
            model_variables = {
                item["id"]: item for item in model["spec"]["processImage"]["variables"]
            }
            device_variable = model_variables.get(variable["deviceVariable"])
            if device_variable is None:
                issues.append(
                    _issue(
                        ErrorCode.REFERENCE_NOT_FOUND,
                        f"{path}.deviceVariable",
                        "Process-image variable does not exist in the device model.",
                    )
                )
            elif device_variable["direction"] != variable["direction"]:
                issues.append(
                    _issue(
                        ErrorCode.FORMAT_INVALID,
                        f"{path}.direction",
                        "Project direction differs from the device model.",
                    )
                )
            end_bit = (
                int(variable["byteOffset"]) * 8
                + int(variable["bitOffset"])
                + int(variable["bitLength"])
            )
            if end_bit > int(process_image[f"{variable['direction']}Bytes"]) * 8:
                issues.append(
                    _issue(
                        ErrorCode.PDO_CAPACITY_EXCEEDED,
                        path,
                        "Mapped project variable exceeds its process-image direction.",
                    )
                )

        task = spec["task"]
        deployment = spec["deployment"]
        if deployment["mode"] == "offline-only":
            if task["buildState"] == "packaged" or any(
                task[field] is not None for field in ("etirSha256", "ecpkgSha256")
            ):
                issues.append(
                    _issue(
                        ErrorCode.READ_ONLY,
                        "$.spec.task",
                        "This issue cannot claim ETIR or ECPKG output in offline-only mode.",
                    )
                )
            if deployment["slot"] != "none" or deployment["signature"] is not None:
                issues.append(
                    _issue(
                        ErrorCode.READ_ONLY,
                        "$.spec.deployment",
                        "Mock-only projects cannot select a slot or deployment signature.",
                    )
                )
        return issues

    def _validate_control_intent(
        self, document: dict[str, Any]
    ) -> list[ValidationIssue]:
        issues: list[ValidationIssue] = []
        spec = document["spec"]
        if _sha256_text(spec["naturalLanguage"]) != spec["naturalLanguageSha256"]:
            issues.append(
                _issue(
                    ErrorCode.FORMAT_INVALID,
                    "$.spec.naturalLanguageSha256",
                    "Natural-language content hash does not match the UTF-8 text.",
                )
            )

        topology_by_position = {
            item["position"]: item
            for item in self.repository.controller["spec"]["topology"]["slaves"]
        }
        bindings: dict[str, dict[str, Any]] = {}
        for index, binding in enumerate(spec["bindings"]):
            path = f"$.spec.bindings[{index}]"
            resource_id = binding["resourceId"]
            if resource_id in bindings:
                issues.append(
                    _issue(
                        ErrorCode.FORMAT_INVALID,
                        f"{path}.resourceId",
                        "Resource ID is duplicated.",
                    )
                )
            bindings[resource_id] = binding
            slave = topology_by_position.get(binding["position"])
            if slave is None or identity_key(slave["identity"]) != identity_key(
                binding["identity"]
            ):
                issues.append(
                    _issue(
                        ErrorCode.UNKNOWN_DEVICE,
                        path,
                        "Binding does not match the immutable mock topology identity.",
                    )
                )
                continue
            model = self.repository.devices.get(slave["deviceModelId"])
            variables = {
                item["id"]: item for item in model["spec"]["processImage"]["variables"]
            }
            variable = variables.get(binding["variable"])
            if variable is None:
                issues.append(
                    _issue(
                        ErrorCode.REFERENCE_NOT_FOUND,
                        f"{path}.variable",
                        "Binding variable is absent from the normalized device model.",
                    )
                )
            elif (
                binding["access"] == "write" and variable["direction"] != "output"
            ) or (
                binding["access"] == "read" and variable["direction"] != "input"
            ):
                issues.append(
                    _issue(
                        ErrorCode.FORMAT_INVALID,
                        f"{path}.access",
                        "Binding access conflicts with process-image direction.",
                    )
                )

        envelopes = {
            item["axisId"]: item for item in spec["safety"]["motionEnvelopes"]
        }
        for axis_id, envelope in envelopes.items():
            if envelope["minimumPosition"] >= envelope["maximumPosition"]:
                issues.append(
                    _issue(
                        ErrorCode.DANGEROUS_MOTION,
                        "$.spec.safety.motionEnvelopes",
                        f"Axis {axis_id} has an empty position envelope.",
                    )
                )
            for field in (
                "maximumVelocity",
                "maximumAcceleration",
                "maximumDeceleration",
            ):
                if envelope[field] <= 0:
                    issues.append(
                        _issue(
                            ErrorCode.DANGEROUS_MOTION,
                            "$.spec.safety.motionEnvelopes",
                            f"Axis {axis_id} requires a positive {field}.",
                        )
                    )

        for index, safe_output in enumerate(spec["safety"]["safeOutputs"]):
            binding = bindings.get(safe_output["resourceId"])
            if (
                binding is None
                or binding["access"] != "write"
                or binding["variable"] != safe_output["variable"]
            ):
                issues.append(
                    _issue(
                        ErrorCode.DANGEROUS_MOTION,
                        f"$.spec.safety.safeOutputs[{index}]",
                        "Safe output must reference the exact declared write binding.",
                    )
                )

        nodes = spec["program"]["nodes"]
        nodes_by_id: dict[str, dict[str, Any]] = {}
        for index, node in enumerate(nodes):
            path = f"$.spec.program.nodes[{index}]"
            node_id = node["nodeId"]
            if node_id in nodes_by_id:
                issues.append(
                    _issue(
                        ErrorCode.FORMAT_INVALID,
                        f"{path}.nodeId",
                        "Program node ID is duplicated.",
                    )
                )
            nodes_by_id[node_id] = node
            for resource in node["resources"]:
                if resource not in bindings:
                    issues.append(
                        _issue(
                            ErrorCode.REFERENCE_NOT_FOUND,
                            f"{path}.resources",
                            f"Node references undeclared resource {resource!r}.",
                        )
                    )
            self._validate_node_parameters(node, path, bindings, envelopes, issues)

        if spec["program"]["entryNodeId"] not in nodes_by_id:
            issues.append(
                _issue(
                    ErrorCode.REFERENCE_NOT_FOUND,
                    "$.spec.program.entryNodeId",
                    "Program entry node does not exist.",
                )
            )
        for index, node in enumerate(nodes):
            for target in node["next"]:
                if target not in nodes_by_id:
                    issues.append(
                        _issue(
                            ErrorCode.REFERENCE_NOT_FOUND,
                            f"$.spec.program.nodes[{index}].next",
                            f"Program edge targets missing node {target!r}.",
                        )
                    )
            body_entry = node["parameters"].get("bodyEntryNodeId")
            if body_entry and body_entry not in nodes_by_id:
                issues.append(
                    _issue(
                        ErrorCode.REFERENCE_NOT_FOUND,
                        f"$.spec.program.nodes[{index}].parameters.bodyEntryNodeId",
                        "Finite-loop body entry node does not exist.",
                    )
                )
        self._reject_unbounded_graph_cycles(nodes_by_id, issues)

        minimum_cycle = self.repository.controller["spec"]["capabilities"][
            "minimumCycleUs"
        ]
        if spec["requestedCycleUs"] < minimum_cycle:
            issues.append(
                _issue(
                    ErrorCode.CYCLE_INFEASIBLE,
                    "$.spec.requestedCycleUs",
                    f"Requested cycle is below controller minimum {minimum_cycle} us.",
                )
            )
        return issues

    def _validate_node_parameters(
        self,
        node: dict[str, Any],
        path: str,
        bindings: dict[str, dict[str, Any]],
        envelopes: dict[str, dict[str, Any]],
        issues: list[ValidationIssue],
    ) -> None:
        op = node["op"]
        parameters = node["parameters"]
        required_by_op = {
            "SetDigitalOutput": {"channel", "value"},
            "SetAnalogOutput": {"channel", "value"},
            "AxisPower": {"enable"},
            "MoveAbsolute": {"position", "velocity", "acceleration", "deceleration"},
            "MoveRelative": {"distance", "velocity", "acceleration", "deceleration"},
            "MoveVelocity": {"velocity", "acceleration", "deceleration"},
            "ControlledStop": {"deceleration"},
            "WaitInput": {"condition"},
            "Repeat": {"loopCount", "bodyEntryNodeId"},
            "RaiseAlarm": {"alarmCode", "severity", "message"},
            "EmitEvent": {"message"},
            "CallFunctionBlock": {"functionBlock"},
            "Branch": {"condition"},
        }
        missing = sorted(required_by_op.get(op, set()) - parameters.keys())
        if missing:
            issues.append(
                _issue(
                    ErrorCode.FORMAT_INVALID,
                    f"{path}.parameters",
                    f"{op} is missing parameters: {', '.join(missing)}.",
                )
            )

        if op == "SetDigitalOutput" and not isinstance(parameters.get("value"), bool):
            issues.append(
                _issue(
                    ErrorCode.FORMAT_INVALID,
                    f"{path}.parameters.value",
                    "SetDigitalOutput requires a Boolean value.",
                )
            )
        if op == "SetAnalogOutput" and (
            not isinstance(parameters.get("value"), (int, float))
            or isinstance(parameters.get("value"), bool)
        ):
            issues.append(
                _issue(
                    ErrorCode.FORMAT_INVALID,
                    f"{path}.parameters.value",
                    "SetAnalogOutput requires a numeric value.",
                )
            )
        if op == "AxisPower" and not isinstance(parameters.get("enable"), bool):
            issues.append(
                _issue(
                    ErrorCode.FORMAT_INVALID,
                    f"{path}.parameters.enable",
                    "AxisPower requires a Boolean enable value.",
                )
            )
        if op == "WaitInput":
            condition = parameters.get("condition", {})
            resource = condition.get("resourceId")
            binding = bindings.get(resource)
            if binding and (
                binding["access"] != "read"
                or binding["variable"] != condition.get("variable")
            ):
                issues.append(
                    _issue(
                        ErrorCode.FORMAT_INVALID,
                        f"{path}.parameters.condition.resourceId",
                        "WaitInput requires a read binding.",
                    )
                )

        motion_ops = {
            "AxisPower",
            "AxisReset",
            "AxisHome",
            "MoveAbsolute",
            "MoveRelative",
            "MoveVelocity",
            "Stop",
            "ControlledStop",
            "WaitAxisDone",
        }
        if op not in motion_ops:
            return
        axis_ids = [resource for resource in node["resources"] if resource in envelopes]
        if len(axis_ids) != 1:
            issues.append(
                _issue(
                    ErrorCode.DANGEROUS_MOTION,
                    f"{path}.resources",
                    "Every motion node must reference exactly one axis with an envelope.",
                )
            )
            return
        envelope = envelopes[axis_ids[0]]
        axis_binding = bindings.get(axis_ids[0])
        if axis_binding:
            topology_slave = next(
                (
                    item
                    for item in self.repository.controller["spec"]["topology"]["slaves"]
                    if item["position"] == axis_binding["position"]
                ),
                None,
            )
            model = (
                self.repository.devices.get(topology_slave["deviceModelId"])
                if topology_slave
                else None
            )
            if model is None or not model["spec"]["cia402"]["supported"]:
                issues.append(
                    _issue(
                        ErrorCode.DANGEROUS_MOTION,
                        f"{path}.resources",
                        "Motion resource is not backed by a CiA402-capable device model.",
                    )
                )
        if node["timeoutMs"] > envelope["maximumDurationMs"]:
            issues.append(
                _issue(
                    ErrorCode.DANGEROUS_MOTION,
                    f"{path}.timeoutMs",
                    "Motion timeout exceeds the approved envelope.",
                )
            )
        limits = {
            "velocity": "maximumVelocity",
            "acceleration": "maximumAcceleration",
            "deceleration": "maximumDeceleration",
        }
        for field, limit_name in limits.items():
            if field in parameters and abs(float(parameters[field])) > float(
                envelope[limit_name]
            ):
                issues.append(
                    _issue(
                        ErrorCode.DANGEROUS_MOTION,
                        f"{path}.parameters.{field}",
                        f"{field} {parameters[field]} exceeds approved "
                        f"{envelope[limit_name]}.",
                    )
                )
            if field in {"acceleration", "deceleration"} and field in parameters:
                if float(parameters[field]) <= 0:
                    issues.append(
                        _issue(
                            ErrorCode.DANGEROUS_MOTION,
                            f"{path}.parameters.{field}",
                            f"{field} must be positive for a bounded motion profile.",
                        )
                    )
        if "position" in parameters and not (
            envelope["minimumPosition"]
            <= parameters["position"]
            <= envelope["maximumPosition"]
        ):
            issues.append(
                _issue(
                    ErrorCode.DANGEROUS_MOTION,
                    f"{path}.parameters.position",
                    "Target position lies outside the approved envelope.",
                )
            )
        if "distance" in parameters and abs(parameters["distance"]) > (
            envelope["maximumPosition"] - envelope["minimumPosition"]
        ):
            issues.append(
                _issue(
                    ErrorCode.DANGEROUS_MOTION,
                    f"{path}.parameters.distance",
                    "Relative distance exceeds the complete approved position span.",
                )
            )

    @staticmethod
    def _reject_unbounded_graph_cycles(
        nodes_by_id: dict[str, dict[str, Any]], issues: list[ValidationIssue]
    ) -> None:
        visiting: set[str] = set()
        visited: set[str] = set()

        def visit(node_id: str, stack: list[str]) -> bool:
            if node_id in visiting:
                start = stack.index(node_id) if node_id in stack else 0
                cycle = " -> ".join([*stack[start:], node_id])
                issues.append(
                    _issue(
                        ErrorCode.FORMAT_INVALID,
                        "$.spec.program.nodes",
                        f"Unbounded next-edge cycle is forbidden: {cycle}. "
                        "Use a finite Repeat node without a back edge.",
                    )
                )
                return True
            if node_id in visited or node_id not in nodes_by_id:
                return False
            visiting.add(node_id)
            stack.append(node_id)
            found = False
            for target in nodes_by_id[node_id]["next"]:
                if visit(target, stack):
                    found = True
                    break
            stack.pop()
            visiting.remove(node_id)
            visited.add(node_id)
            return found

        for node_id in nodes_by_id:
            if visit(node_id, []):
                break


def validation_report(issues: list[ValidationIssue]) -> dict[str, Any]:
    return {
        "valid": not issues,
        "issueCount": len(issues),
        "issues": [issue.as_dict() for issue in issues],
    }


def canonical_parameters_sha256(parameters: dict[str, Any]) -> str:
    payload = json.dumps(
        parameters, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()
