"""Immutable loader for the checked-in mock controller and device registry."""

from __future__ import annotations

import copy
import hashlib
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import urlparse
from xml.etree import ElementTree

from .schema import SchemaRegistry, load_json


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def identity_key(identity: dict[str, Any]) -> tuple[int, int, int]:
    return (
        int(identity["vendorId"]),
        int(identity["productCode"]),
        int(identity["revision"]),
    )


def identity_in_scope(
    identity: dict[str, Any], scope: dict[str, Any]
) -> bool:
    if int(identity["vendorId"]) != int(scope["vendorId"]):
        return False
    if int(identity["productCode"]) != int(scope["productCode"]):
        return False
    revision = int(identity["revision"])
    if not int(scope["minimumRevision"]) <= revision <= int(scope["maximumRevision"]):
        return False
    serial_allowlist = scope.get("serialAllowlist")
    if serial_allowlist:
        return int(identity.get("serial", 0)) in serial_allowlist
    return True


def _esi_unsigned(value: str) -> int:
    stripped = value.strip()
    if stripped.lower().startswith("#x"):
        return int(stripped[2:], 16)
    return int(stripped, 0)


def _local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def esi_identity(path: Path) -> tuple[int, int, int]:
    root = ElementTree.parse(path).getroot()
    if _local_name(root.tag) != "EtherCATInfo":
        raise ValueError(f"{path} does not have an EtherCATInfo root")
    vendor_id: int | None = None
    product_code: int | None = None
    revision: int | None = None
    for element in root.iter():
        if _local_name(element.tag) == "Vendor":
            for child in element:
                if _local_name(child.tag) == "Id" and child.text:
                    vendor_id = _esi_unsigned(child.text)
                    break
        if _local_name(element.tag) == "Type":
            product = element.attrib.get("ProductCode")
            revision_text = element.attrib.get("RevisionNo")
            if product and revision_text:
                product_code = _esi_unsigned(product)
                revision = _esi_unsigned(revision_text)
                break
    if vendor_id is None or product_code is None or revision is None:
        raise ValueError(f"{path} does not contain a complete ESI identity")
    return vendor_id, product_code, revision


class MockRepository:
    """Loads only repository files; it never discovers or writes a device."""

    def __init__(self, root: Path | None = None):
        self.root = (root or project_root()).resolve()
        self.schema_registry = SchemaRegistry(self.root / "schemas" / "v1")
        self.controller = load_json(self.root / "mock" / "controller.json")
        self.project = load_json(
            self.root / "mock" / "project" / "controller-project.json"
        )
        self.intent = load_json(
            self.root / "mock" / "intents" / "safe-servo-intent.json"
        )

        self.devices: dict[str, dict[str, Any]] = {}
        self.device_file_sha256: dict[str, str] = {}
        self.esi_file_sha256: dict[str, str] = {}
        for path in sorted((self.root / "mock" / "devices").glob("*.json")):
            document = load_json(path)
            document_id = document["metadata"]["id"]
            if document_id in self.devices:
                raise ValueError(f"duplicate mock device model id: {document_id}")
            self.devices[document_id] = document
            self.device_file_sha256[document_id] = file_sha256(path)
            esi_source = document["spec"]["esiSource"]
            parsed_uri = urlparse(esi_source["uri"])
            if parsed_uri.scheme != "mock" or parsed_uri.netloc != "esi":
                raise ValueError(
                    f"mock device {document_id} has a non-mock ESI source"
                )
            esi_path = self.root / "mock" / "esi" / Path(parsed_uri.path).name
            actual_esi_hash = file_sha256(esi_path)
            if actual_esi_hash != esi_source["sha256"]:
                raise ValueError(f"ESI hash mismatch for mock device {document_id}")
            if esi_identity(esi_path) != identity_key(document["spec"]["identity"]):
                raise ValueError(f"ESI identity mismatch for mock device {document_id}")
            self.esi_file_sha256[document_id] = actual_esi_hash

        self.adapters: dict[str, dict[str, Any]] = {}
        self.adapter_file_sha256: dict[str, str] = {}
        for path in sorted((self.root / "mock" / "adapters").glob("*.json")):
            document = load_json(path)
            document_id = document["metadata"]["id"]
            if document_id in self.adapters:
                raise ValueError(f"duplicate mock adapter id: {document_id}")
            self.adapters[document_id] = document
            self.adapter_file_sha256[document_id] = file_sha256(path)

        fixture_documents = [
            *self.devices.values(),
            *self.adapters.values(),
            self.project,
            self.intent,
        ]
        fixture_issues = [
            issue
            for document in fixture_documents
            for issue in self.schema_registry.validate(document)
        ]
        if fixture_issues:
            rendered = "; ".join(
                f"{issue.code} {issue.path}: {issue.message}"
                for issue in fixture_issues[:10]
            )
            raise ValueError(f"invalid checked-in mock fixture: {rendered}")

    def clone_with(
        self,
        *,
        adapters: Iterable[dict[str, Any]] | None = None,
        devices: Iterable[dict[str, Any]] | None = None,
    ) -> "MockRepository":
        clone = copy.copy(self)
        clone.controller = copy.deepcopy(self.controller)
        clone.project = copy.deepcopy(self.project)
        clone.intent = copy.deepcopy(self.intent)
        clone.devices = copy.deepcopy(self.devices)
        clone.device_file_sha256 = dict(self.device_file_sha256)
        clone.esi_file_sha256 = dict(self.esi_file_sha256)
        clone.adapters = copy.deepcopy(self.adapters)
        clone.adapter_file_sha256 = dict(self.adapter_file_sha256)
        if devices is not None:
            clone.devices = {
                document["metadata"]["id"]: copy.deepcopy(document)
                for document in devices
            }
        if adapters is not None:
            clone.adapters = {
                document["metadata"]["id"]: copy.deepcopy(document)
                for document in adapters
            }
        return clone

    def controller_copy(self) -> dict[str, Any]:
        return copy.deepcopy(self.controller)

    def project_copy(self) -> dict[str, Any]:
        return copy.deepcopy(self.project)

    def intent_copy(self) -> dict[str, Any]:
        return copy.deepcopy(self.intent)

    def device_copy(self, device_id: str) -> dict[str, Any] | None:
        document = self.devices.get(device_id)
        return copy.deepcopy(document) if document else None

    def adapter_copy(self, adapter_id: str) -> dict[str, Any] | None:
        document = self.adapters.get(adapter_id)
        return copy.deepcopy(document) if document else None

    def device_at_position(self, position: int) -> dict[str, Any] | None:
        slaves = self.controller["spec"]["topology"]["slaves"]
        for slave in slaves:
            if slave["position"] == position:
                return self.device_copy(slave["deviceModelId"])
        return None

    def adapter_candidates(
        self, identity: dict[str, Any], esi_sha256: str | None = None
    ) -> list[dict[str, Any]]:
        candidates: list[dict[str, Any]] = []
        for adapter in self.adapters.values():
            if adapter["spec"]["validationState"] == "revoked":
                continue
            if not identity_in_scope(identity, adapter["spec"]["identityScope"]):
                continue
            if esi_sha256 and esi_sha256 not in adapter["spec"]["esiSha256"]:
                continue
            candidates.append(adapter)
        return sorted(
            candidates,
            key=lambda item: (
                -int(item["spec"]["matchPriority"]),
                item["metadata"]["id"],
            ),
        )

    def protocol_document(self) -> dict[str, Any]:
        return {
            "apiVersion": "controller-tools/v1",
            "mcpProtocolVersion": "2025-11-25",
            "openApiVersion": "3.1.1",
            "jsonSchemaDialect": "https://json-schema.org/draft/2020-12/schema",
            "dataApiVersion": "controller.embed-labs.dev/v1",
            "readOnly": True,
            "mockOnly": True,
            "stateChangingTools": [],
        }
