from __future__ import annotations

import unittest

from controller_tools_v1 import ContractValidator, MockRepository
from controller_tools_v1.errors import ErrorCode


class FailureFirstSafetyTests(unittest.TestCase):
    """Acceptance failures are listed before happy-path regression tests."""

    def setUp(self) -> None:
        self.repository = MockRepository()
        self.validator = ContractValidator(self.repository)

    @staticmethod
    def codes(issues) -> set[str]:
        return {issue.code for issue in issues}

    def test_00_unknown_device_is_rejected(self) -> None:
        project = self.repository.project_copy()
        slave = project["spec"]["topology"]["slaves"][0]
        slave["identity"]["productCode"] = 4294967294
        issues = self.validator.validate(project)
        self.assertIn(ErrorCode.UNKNOWN_DEVICE.value, self.codes(issues))

    def test_01_overlapping_esi_scope_is_rejected(self) -> None:
        conflicting = self.repository.adapter_copy("mock.adapter.digital-io.8x8")
        self.assertIsNotNone(conflicting)
        conflicting["metadata"]["id"] = "mock.adapter.digital-io-conflict"
        conflicting["spec"]["esiSha256"] = ["f" * 64]
        adapters = [*self.repository.adapters.values(), conflicting]
        repository = self.repository.clone_with(adapters=adapters)
        issues = ContractValidator(repository).validate(repository.project)
        self.assertIn(ErrorCode.ESI_CONFLICT.value, self.codes(issues))

    def test_02_pdo_capacity_overrun_is_rejected(self) -> None:
        project = self.repository.project_copy()
        project["spec"]["processImage"]["outputBytes"] = 513
        issues = self.validator.validate(project)
        self.assertIn(ErrorCode.PDO_CAPACITY_EXCEEDED.value, self.codes(issues))

    def test_03_infeasible_cycle_is_rejected_with_budget_context(self) -> None:
        project = self.repository.project_copy()
        project["spec"]["timing"]["cycleUs"] = 125
        issues = self.validator.validate(project)
        cycle_issues = [
            issue
            for issue in issues
            if issue.code == ErrorCode.CYCLE_INFEASIBLE.value
        ]
        self.assertTrue(cycle_issues)
        self.assertTrue(
            any(
                "required" in issue.message
                or "suggested cycle" in issue.message
                or "not listed" in issue.message
                for issue in cycle_issues
            )
        )

    def test_04_dangerous_motion_velocity_is_rejected(self) -> None:
        intent = self.repository.intent_copy()
        move = next(
            node
            for node in intent["spec"]["program"]["nodes"]
            if node["op"] == "MoveVelocity"
        )
        move["parameters"]["velocity"] = 5001
        issues = self.validator.validate(intent)
        self.assertIn(ErrorCode.DANGEROUS_MOTION.value, self.codes(issues))

    def test_05_motion_without_timeout_is_rejected_structurally(self) -> None:
        intent = self.repository.intent_copy()
        move = next(
            node
            for node in intent["spec"]["program"]["nodes"]
            if node["op"] == "MoveVelocity"
        )
        del move["timeoutMs"]
        issues = self.validator.validate(intent)
        self.assertIn("SCHEMA_REQUIRED", self.codes(issues))

    def test_06_mock_adapter_cannot_cross_hardware_boundary(self) -> None:
        project = self.repository.project_copy()
        project["spec"]["topology"]["source"] = "controller-scan"
        project["spec"]["deployment"]["mode"] = "stage-candidate"
        project["spec"]["deployment"]["slot"] = "A"
        issues = self.validator.validate(project)
        self.assertIn(ErrorCode.ADAPTER_UNVERIFIED.value, self.codes(issues))

    def test_07_unknown_field_is_rejected(self) -> None:
        project = self.repository.project_copy()
        project["spec"]["controller"]["rawRegister"] = 1
        issues = self.validator.validate(project)
        self.assertIn("SCHEMA_ADDITIONAL_PROPERTY", self.codes(issues))

    def test_08_unbounded_control_flow_cycle_is_rejected(self) -> None:
        intent = self.repository.intent_copy()
        final_node = next(
            node
            for node in intent["spec"]["program"]["nodes"]
            if node["nodeId"] == "reset-output"
        )
        final_node["next"] = ["enable-axis"]
        issues = self.validator.validate(intent)
        self.assertIn(ErrorCode.FORMAT_INVALID.value, self.codes(issues))
        self.assertTrue(any("Unbounded" in issue.message for issue in issues))


if __name__ == "__main__":
    unittest.main()
