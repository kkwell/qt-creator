from __future__ import annotations

import unittest

from controller_tools_v1 import ContractValidator, MockRepository, ReadOnlyGateway
from controller_tools_v1.repository import load_json


class ContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.repository = MockRepository()

    def test_schema_registry_qualifies_without_remote_access(self) -> None:
        self.assertEqual(self.repository.schema_registry.qualify_schemas(), [])
        self.assertEqual(
            set(self.repository.schema_registry.schema_names),
            {
                "adapter-manifest.schema.json",
                "common.schema.json",
                "control-intent.schema.json",
                "controller-project.schema.json",
                "normalized-device-model.schema.json",
            },
        )

    def test_all_checked_in_artifacts_are_valid(self) -> None:
        self.assertEqual(ContractValidator(self.repository).validate_repository(), [])

    def test_adapter_model_and_project_manifest_hashes_are_exact(self) -> None:
        for adapter_id, adapter in self.repository.adapters.items():
            model_id = adapter["spec"]["deviceModel"]["id"]
            self.assertEqual(
                adapter["spec"]["deviceModel"]["sha256"],
                self.repository.device_file_sha256[model_id],
            )
            self.assertEqual(
                adapter["spec"]["esiSha256"],
                [self.repository.esi_file_sha256[model_id]],
            )
        for slave in self.repository.project["spec"]["topology"]["slaves"]:
            adapter_id = slave["adapter"]["adapterId"]
            self.assertEqual(
                slave["adapter"]["manifestSha256"],
                self.repository.adapter_file_sha256[adapter_id],
            )

    def test_tool_catalog_is_closed_and_read_only(self) -> None:
        gateway = ReadOnlyGateway(self.repository)
        self.assertEqual(len(gateway.tool_definitions), 9)
        for tool in gateway.tool_definitions:
            annotations = tool["annotations"]
            self.assertTrue(annotations["readOnlyHint"], tool["name"])
            self.assertFalse(annotations["destructiveHint"], tool["name"])
            self.assertTrue(annotations["idempotentHint"], tool["name"])
            lowered = tool["name"].lower()
            for forbidden in ("scan", "deploy", "activate", "start", "move", "sdo"):
                self.assertNotIn(forbidden, lowered)

    def test_openapi_and_mcp_publish_the_same_controller_tools(self) -> None:
        openapi = load_json(
            self.repository.root / "api" / "controller-tools-v1.openapi.json"
        )
        catalog = load_json(
            self.repository.root / "api" / "controller-tools-v1.mcp-tools.json"
        )
        self.assertEqual(openapi["openapi"], "3.1.1")
        self.assertEqual(
            openapi["jsonSchemaDialect"],
            "https://json-schema.org/draft/2020-12/schema",
        )
        operations = {
            operation["operationId"]
            for path, path_item in openapi["paths"].items()
            if path.startswith("/api/controller-tools/v1")
            for method, operation in path_item.items()
            if method in {"get", "post"}
            and operation["operationId"] != "gateway.get-openapi"
        }
        self.assertEqual(operations, {tool["name"] for tool in catalog["tools"]})
        boundary = openapi["x-safety-boundary"]
        self.assertTrue(boundary["controllerViews"]["mockOnly"])
        self.assertTrue(boundary["controllerViews"]["readOnly"])
        self.assertTrue(boundary["semanticRuntime"]["approvalRequired"])
        self.assertFalse(boundary["semanticRuntime"]["automationCanApprove"])
        self.assertFalse(boundary["semanticRuntime"]["directProviderCalls"])
        self.assertFalse(boundary["motion"])

    def test_versions_are_explicit_and_independent(self) -> None:
        protocol = self.repository.protocol_document()
        self.assertEqual(protocol["apiVersion"], "controller-tools/v1")
        self.assertEqual(protocol["mcpProtocolVersion"], "2025-11-25")
        self.assertEqual(protocol["openApiVersion"], "3.1.1")
        self.assertEqual(
            protocol["dataApiVersion"], "controller.embed-labs.dev/v1"
        )

    def test_mock_repository_contains_three_device_classes(self) -> None:
        capabilities = {
            capability
            for adapter in self.repository.adapters.values()
            for capability in adapter["spec"]["capabilities"]
        }
        self.assertTrue({"digital-input", "analog-input", "cia402"} <= capabilities)
        self.assertEqual(
            self.repository.controller["spec"]["topology"]["respondingCount"], 3
        )
        self.assertTrue(
            all(
                not adapter["spec"]["realHardwareAllowed"]
                for adapter in self.repository.adapters.values()
            )
        )

    def test_knowledge_graph_binds_completed_gateway_issue(self) -> None:
        graph = load_json(
            self.repository.root / "knowledge-graph" / "graph.json"
        )
        nodes = {node["id"]: node for node in graph["nodes"]}
        self.assertEqual(len(nodes), len(graph["nodes"]))
        current = nodes["ISSUE-AI-CONTROLLER-001"]
        self.assertEqual(current["status"], "qualified")
        self.assertEqual(current["scopeRoot"], "ethercat-ai-controller")
        self.assertFalse(current["realHardwareAllowed"])
        unresolved = [
            node["id"]
            for node in graph["nodes"]
            if node["type"] == "issue" and node["status"] == "unresolved"
        ]
        self.assertEqual(unresolved, [])
        self.assertIsNone(graph["nextIssue"])
        for node in graph["nodes"]:
            if node["type"] == "data-format":
                self.assertTrue((self.repository.root / node["path"]).is_file())


if __name__ == "__main__":
    unittest.main()
