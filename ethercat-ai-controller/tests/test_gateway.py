from __future__ import annotations

import unittest

from controller_tools_v1 import MockRepository, ReadOnlyGateway
from controller_tools_v1.errors import ErrorCode
from controller_tools_v1.gateway import Actor


class GatewayTests(unittest.TestCase):
    def setUp(self) -> None:
        self.repository = MockRepository()
        self.gateway = ReadOnlyGateway(self.repository)
        self.actor = Actor("test-user", "test-ai/1.0", "test-session")

    def test_controller_reads_return_mock_evidence(self) -> None:
        controller_id = self.repository.controller["spec"]["controllerId"]
        for tool in (
            "controller.get-capabilities",
            "controller.get-state",
            "controller.get-topology",
            "controller.get-diagnostics",
        ):
            result = self.gateway.dispatch(
                tool, {"controllerId": controller_id}, self.actor
            )
            self.assertTrue(result["ok"], result)
            self.assertTrue(result["audit"]["mock"])
            self.assertTrue(result["audit"]["readOnly"])
            self.assertEqual(
                result["audit"]["beforeStateSha256"],
                result["audit"]["afterStateSha256"],
            )
            self.assertIsNone(result["audit"]["leaseId"])

    def test_each_mock_device_is_readable_by_position(self) -> None:
        controller_id = self.repository.controller["spec"]["controllerId"]
        kinds = []
        for position in range(3):
            result = self.gateway.dispatch(
                "controller.get-device",
                {"controllerId": controller_id, "position": position},
                self.actor,
            )
            self.assertTrue(result["ok"], result)
            model = result["data"]["normalizedDeviceModel"]
            self.assertEqual(model["kind"], "NormalizedDeviceModel")
            kinds.extend(
                self.repository.adapters[
                    result["data"]["topologyIdentity"]["adapterId"]
                ]["spec"]["capabilities"]
            )
        self.assertIn("digital-output", kinds)
        self.assertIn("analog-output", kinds)
        self.assertIn("cia402", kinds)

    def test_invalid_arguments_are_tool_errors(self) -> None:
        result = self.gateway.dispatch("controller.get-device", {"position": -1})
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], ErrorCode.BAD_REQUEST.value)
        self.assertIn("issues", result["error"]["details"])

    def test_unknown_state_changing_tool_is_not_exposed(self) -> None:
        result = self.gateway.dispatch("controller.scan", {})
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], ErrorCode.NOT_FOUND.value)

    def test_artifact_validation_is_pure_and_reports_failure(self) -> None:
        artifact = self.repository.intent_copy()
        move = next(
            node
            for node in artifact["spec"]["program"]["nodes"]
            if node["op"] == "MoveVelocity"
        )
        move["parameters"]["velocity"] = 999999
        before = self.repository.intent_copy()
        result = self.gateway.dispatch(
            "artifact.validate", {"artifact": artifact}, self.actor
        )
        self.assertTrue(result["ok"])
        self.assertFalse(result["data"]["report"]["valid"])
        self.assertEqual(result["data"]["stored"], False)
        self.assertEqual(result["data"]["deployed"], False)
        self.assertEqual(self.repository.intent, before)

    def test_caller_cannot_mutate_repository_through_result(self) -> None:
        result = self.gateway.dispatch("adapter.list", {}, self.actor)
        self.assertTrue(result["ok"])
        result["data"]["adapters"][0]["validationState"] = "verified"
        self.assertTrue(
            all(
                adapter["spec"]["validationState"] == "mock-only"
                for adapter in self.repository.adapters.values()
            )
        )

    def test_operation_id_makes_retries_observably_idempotent(self) -> None:
        arguments = {"operationId": "test-operation-001"}
        first = self.gateway.dispatch("gateway.get-protocol", arguments, self.actor)
        second = self.gateway.dispatch("gateway.get-protocol", arguments, self.actor)
        self.assertEqual(first, second)
        self.assertEqual(first["operationId"], "test-operation-001")
        self.assertEqual(len(self.gateway.audit_events), 2)

    def test_every_tool_result_matches_its_mcp_output_schema(self) -> None:
        controller_id = self.repository.controller["spec"]["controllerId"]
        arguments = {
            "controller.list": {},
            "controller.get-capabilities": {"controllerId": controller_id},
            "controller.get-state": {"controllerId": controller_id},
            "controller.get-topology": {"controllerId": controller_id},
            "controller.get-device": {
                "controllerId": controller_id,
                "position": 0,
            },
            "controller.get-diagnostics": {"controllerId": controller_id},
            "adapter.list": {},
            "artifact.validate": {"artifact": self.repository.intent_copy()},
            "gateway.get-protocol": {},
        }
        for tool in self.gateway.tool_definitions:
            result = self.gateway.dispatch(
                tool["name"], arguments[tool["name"]], self.actor
            )
            issues = self.repository.schema_registry.validate_inline(
                result, tool["outputSchema"]
            )
            self.assertEqual(issues, [], tool["name"])


if __name__ == "__main__":
    unittest.main()
