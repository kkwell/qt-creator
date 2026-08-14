"""Small, deterministic JSON Schema 2020-12 subset used by this issue.

The repository intentionally has no network-fetched schema or Python package
dependency. The validator implements only the assertion vocabulary used by
the checked-in contracts. Unknown assertion keywords are rejected when the
schema registry is qualified by tests.
"""

from __future__ import annotations

import json
import re
import uuid
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import urldefrag, urljoin, urlparse


SUPPORTED_ASSERTIONS = {
    "$defs",
    "$id",
    "$ref",
    "$schema",
    "additionalProperties",
    "allOf",
    "anyOf",
    "const",
    "description",
    "dependentRequired",
    "enum",
    "examples",
    "format",
    "items",
    "maxItems",
    "maxLength",
    "maxProperties",
    "maximum",
    "minItems",
    "minLength",
    "minProperties",
    "minimum",
    "multipleOf",
    "not",
    "oneOf",
    "pattern",
    "properties",
    "required",
    "title",
    "type",
    "uniqueItems",
}


@dataclass(frozen=True)
class ValidationIssue:
    code: str
    path: str
    message: str

    def as_dict(self) -> dict[str, str]:
        return {"code": self.code, "path": self.path, "message": self.message}


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _json_equal(left: Any, right: Any) -> bool:
    return json.dumps(left, sort_keys=True, separators=(",", ":")) == json.dumps(
        right, sort_keys=True, separators=(",", ":")
    )


def _matches_type(value: Any, expected: str) -> bool:
    if expected == "null":
        return value is None
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "string":
        return isinstance(value, str)
    if expected == "array":
        return isinstance(value, list)
    if expected == "object":
        return isinstance(value, dict)
    return False


class SchemaRegistry:
    """Loads and validates the fixed schema set without remote resolution."""

    def __init__(self, schema_directory: Path):
        self.schema_directory = schema_directory.resolve()
        self._by_id: dict[str, dict[str, Any]] = {}
        self._by_name: dict[str, dict[str, Any]] = {}
        self._source_by_object: dict[int, str] = {}
        for path in sorted(self.schema_directory.glob("*.schema.json")):
            document = load_json(path)
            if not isinstance(document, dict):
                raise ValueError(f"{path} does not contain a JSON object")
            schema_id = document.get("$id")
            if not isinstance(schema_id, str) or not schema_id:
                raise ValueError(f"{path} has no non-empty $id")
            if schema_id in self._by_id:
                raise ValueError(f"duplicate schema id: {schema_id}")
            self._by_id[schema_id] = document
            self._by_name[path.name] = document
            self._source_by_object[id(document)] = schema_id

    @property
    def schema_names(self) -> tuple[str, ...]:
        return tuple(sorted(self._by_name))

    def schema(self, name_or_id: str) -> dict[str, Any]:
        schema = self._by_id.get(name_or_id) or self._by_name.get(name_or_id)
        if schema is None:
            raise KeyError(name_or_id)
        return schema

    def schema_for_document(self, document: Any) -> dict[str, Any] | None:
        if not isinstance(document, dict):
            return None
        kind = document.get("kind")
        for schema in self._by_id.values():
            kind_schema = schema.get("properties", {}).get("kind", {})
            if kind_schema.get("const") == kind:
                return schema
        return None

    def validate(
        self, document: Any, schema_name_or_id: str | None = None
    ) -> list[ValidationIssue]:
        if schema_name_or_id:
            schema = self.schema(schema_name_or_id)
        else:
            schema = self.schema_for_document(document)
            if schema is None:
                return [
                    ValidationIssue(
                        "SCHEMA_KIND_UNKNOWN",
                        "$.kind",
                        "No registered schema matches the document kind.",
                    )
                ]
        issues: list[ValidationIssue] = []
        base_uri = self._source_by_object[id(schema)]
        self._validate_value(document, schema, "$", schema, base_uri, issues, 0)
        return issues

    def validate_inline(
        self, value: Any, schema: dict[str, Any]
    ) -> list[ValidationIssue]:
        """Validate a value against a self-contained MCP/OpenAPI schema."""
        issues: list[ValidationIssue] = []
        self._validate_value(
            value, schema, "$", schema, "urn:controller-tools:inline", issues, 0
        )
        return issues

    def qualify_schemas(self) -> list[ValidationIssue]:
        issues: list[ValidationIssue] = []
        for name, schema in sorted(self._by_name.items()):
            if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
                issues.append(
                    ValidationIssue(
                        "SCHEMA_DIALECT",
                        f"{name}.$schema",
                        "Schema must declare JSON Schema Draft 2020-12.",
                    )
                )
            self._check_keywords(schema, f"{name}.$", issues)
            try:
                self._walk_refs(schema, schema, self._source_by_object[id(schema)], set())
            except (KeyError, ValueError) as error:
                issues.append(
                    ValidationIssue("SCHEMA_REF_INVALID", f"{name}.$", str(error))
                )
        return issues

    def _check_keywords(
        self, schema: Any, path: str, issues: list[ValidationIssue]
    ) -> None:
        if not isinstance(schema, dict):
            return
        for key, value in schema.items():
            if key not in SUPPORTED_ASSERTIONS and not key.startswith("x-"):
                issues.append(
                    ValidationIssue(
                        "SCHEMA_KEYWORD_UNSUPPORTED",
                        f"{path}.{key}",
                        f"Validator does not implement schema keyword {key!r}.",
                    )
                )
            if key in {"properties", "$defs"} and isinstance(value, dict):
                for child_name, child in value.items():
                    self._check_keywords(child, f"{path}.{key}.{child_name}", issues)
            elif key in {"items", "additionalProperties", "not"}:
                self._check_keywords(value, f"{path}.{key}", issues)
            elif key in {"allOf", "anyOf", "oneOf"} and isinstance(value, list):
                for index, child in enumerate(value):
                    self._check_keywords(child, f"{path}.{key}[{index}]", issues)

    def _walk_refs(
        self,
        schema: Any,
        root_schema: dict[str, Any],
        base_uri: str,
        seen: set[tuple[str, str]],
    ) -> None:
        if isinstance(schema, list):
            for child in schema:
                self._walk_refs(child, root_schema, base_uri, seen)
            return
        if not isinstance(schema, dict):
            return
        nested_base = schema.get("$id", base_uri)
        ref = schema.get("$ref")
        if isinstance(ref, str):
            key = (nested_base, ref)
            if key not in seen:
                seen.add(key)
                target, target_root, target_base = self._resolve_ref(
                    ref, root_schema, nested_base
                )
                self._walk_refs(target, target_root, target_base, seen)
        for key, child in schema.items():
            if key not in {"$ref", "$id"}:
                self._walk_refs(child, root_schema, nested_base, seen)

    def _resolve_ref(
        self, ref: str, current_root: dict[str, Any], base_uri: str
    ) -> tuple[dict[str, Any], dict[str, Any], str]:
        target_part, fragment = urldefrag(ref)
        if target_part:
            absolute = urljoin(base_uri, target_part)
            target_root = self._by_id.get(absolute)
            if target_root is None:
                target_root = self._by_name.get(Path(target_part).name)
            if target_root is None:
                raise KeyError(f"unresolved schema reference: {ref}")
            target_base = self._source_by_object[id(target_root)]
        else:
            target_root = current_root
            target_base = base_uri

        target: Any = target_root
        if fragment:
            if not fragment.startswith("/"):
                raise ValueError(f"unsupported non-pointer schema fragment: {ref}")
            for raw_token in fragment[1:].split("/"):
                token = raw_token.replace("~1", "/").replace("~0", "~")
                if not isinstance(target, dict) or token not in target:
                    raise KeyError(f"unresolved JSON pointer in schema reference: {ref}")
                target = target[token]
        if not isinstance(target, dict):
            raise ValueError(f"schema reference does not resolve to an object: {ref}")
        return target, target_root, target_base

    def _validate_value(
        self,
        value: Any,
        schema: dict[str, Any],
        path: str,
        root_schema: dict[str, Any],
        base_uri: str,
        issues: list[ValidationIssue],
        depth: int,
    ) -> None:
        if depth > 128:
            issues.append(
                ValidationIssue(
                    "SCHEMA_DEPTH_EXCEEDED",
                    path,
                    "Document or schema reference nesting exceeds 128 levels.",
                )
            )
            return

        nested_base = schema.get("$id", base_uri)
        ref = schema.get("$ref")
        if isinstance(ref, str):
            try:
                target, target_root, target_base = self._resolve_ref(
                    ref, root_schema, nested_base
                )
            except (KeyError, ValueError) as error:
                issues.append(ValidationIssue("SCHEMA_REF_INVALID", path, str(error)))
                return
            self._validate_value(
                value,
                target,
                path,
                target_root,
                target_base,
                issues,
                depth + 1,
            )

        expected_type = schema.get("type")
        if expected_type is not None:
            expected_types = (
                list(expected_type)
                if isinstance(expected_type, list)
                else [expected_type]
            )
            if not any(_matches_type(value, item) for item in expected_types):
                issues.append(
                    ValidationIssue(
                        "SCHEMA_TYPE",
                        path,
                        f"Expected type {expected_type!r}, got {type(value).__name__}.",
                    )
                )
                return

        if "const" in schema and not _json_equal(value, schema["const"]):
            issues.append(
                ValidationIssue(
                    "SCHEMA_CONST",
                    path,
                    f"Value must equal {schema['const']!r}.",
                )
            )
        if "enum" in schema and not any(
            _json_equal(value, candidate) for candidate in schema["enum"]
        ):
            issues.append(
                ValidationIssue(
                    "SCHEMA_ENUM",
                    path,
                    f"Value is not one of {schema['enum']!r}.",
                )
            )

        for keyword, match_count in (("allOf", None), ("anyOf", 1), ("oneOf", 1)):
            branches = schema.get(keyword)
            if not isinstance(branches, list):
                continue
            branch_results: list[list[ValidationIssue]] = []
            for branch in branches:
                branch_issues: list[ValidationIssue] = []
                self._validate_value(
                    value,
                    branch,
                    path,
                    root_schema,
                    nested_base,
                    branch_issues,
                    depth + 1,
                )
                branch_results.append(branch_issues)
            matches = sum(not result for result in branch_results)
            valid = matches == len(branches) if keyword == "allOf" else matches >= match_count
            if keyword == "oneOf":
                valid = matches == 1
            if not valid:
                issues.append(
                    ValidationIssue(
                        f"SCHEMA_{keyword.upper()}",
                        path,
                        f"Value matched {matches} of {len(branches)} {keyword} branches.",
                    )
                )

        if "not" in schema and isinstance(schema["not"], dict):
            forbidden_issues: list[ValidationIssue] = []
            self._validate_value(
                value,
                schema["not"],
                path,
                root_schema,
                nested_base,
                forbidden_issues,
                depth + 1,
            )
            if not forbidden_issues:
                issues.append(
                    ValidationIssue(
                        "SCHEMA_NOT", path, "Value matches a forbidden schema."
                    )
                )

        if isinstance(value, dict):
            self._validate_object(
                value, schema, path, root_schema, nested_base, issues, depth
            )
        elif isinstance(value, list):
            self._validate_array(
                value, schema, path, root_schema, nested_base, issues, depth
            )
        elif isinstance(value, str):
            self._validate_string(value, schema, path, issues)
        elif isinstance(value, (int, float)) and not isinstance(value, bool):
            self._validate_number(value, schema, path, issues)

    def _validate_object(
        self,
        value: dict[str, Any],
        schema: dict[str, Any],
        path: str,
        root_schema: dict[str, Any],
        base_uri: str,
        issues: list[ValidationIssue],
        depth: int,
    ) -> None:
        required = schema.get("required", [])
        for key in required:
            if key not in value:
                issues.append(
                    ValidationIssue(
                        "SCHEMA_REQUIRED",
                        f"{path}.{key}",
                        f"Required property {key!r} is missing.",
                    )
                )
        properties = schema.get("properties", {})
        if isinstance(properties, dict):
            for key, child_schema in properties.items():
                if key in value and isinstance(child_schema, dict):
                    self._validate_value(
                        value[key],
                        child_schema,
                        f"{path}.{key}",
                        root_schema,
                        base_uri,
                        issues,
                        depth + 1,
                    )
        additional = schema.get("additionalProperties", True)
        for key in value.keys() - properties.keys():
            extra_path = f"{path}.{key}"
            if additional is False:
                issues.append(
                    ValidationIssue(
                        "SCHEMA_ADDITIONAL_PROPERTY",
                        extra_path,
                        f"Property {key!r} is not allowed.",
                    )
                )
            elif isinstance(additional, dict):
                self._validate_value(
                    value[key],
                    additional,
                    extra_path,
                    root_schema,
                    base_uri,
                    issues,
                    depth + 1,
                )
        minimum = schema.get("minProperties")
        maximum = schema.get("maxProperties")
        if isinstance(minimum, int) and len(value) < minimum:
            issues.append(
                ValidationIssue(
                    "SCHEMA_MIN_PROPERTIES",
                    path,
                    f"Object has {len(value)} properties; minimum is {minimum}.",
                )
            )
        if isinstance(maximum, int) and len(value) > maximum:
            issues.append(
                ValidationIssue(
                    "SCHEMA_MAX_PROPERTIES",
                    path,
                    f"Object has {len(value)} properties; maximum is {maximum}.",
                )
            )
        dependent = schema.get("dependentRequired", {})
        if isinstance(dependent, dict):
            for key, dependencies in dependent.items():
                if key in value:
                    for dependency in dependencies:
                        if dependency not in value:
                            issues.append(
                                ValidationIssue(
                                    "SCHEMA_DEPENDENT_REQUIRED",
                                    f"{path}.{dependency}",
                                    f"{dependency!r} is required when {key!r} is present.",
                                )
                            )

    def _validate_array(
        self,
        value: list[Any],
        schema: dict[str, Any],
        path: str,
        root_schema: dict[str, Any],
        base_uri: str,
        issues: list[ValidationIssue],
        depth: int,
    ) -> None:
        minimum = schema.get("minItems")
        maximum = schema.get("maxItems")
        if isinstance(minimum, int) and len(value) < minimum:
            issues.append(
                ValidationIssue(
                    "SCHEMA_MIN_ITEMS",
                    path,
                    f"Array has {len(value)} items; minimum is {minimum}.",
                )
            )
        if isinstance(maximum, int) and len(value) > maximum:
            issues.append(
                ValidationIssue(
                    "SCHEMA_MAX_ITEMS",
                    path,
                    f"Array has {len(value)} items; maximum is {maximum}.",
                )
            )
        if schema.get("uniqueItems"):
            canonical = [
                json.dumps(item, sort_keys=True, separators=(",", ":")) for item in value
            ]
            if len(canonical) != len(set(canonical)):
                issues.append(
                    ValidationIssue(
                        "SCHEMA_UNIQUE_ITEMS", path, "Array items must be unique."
                    )
                )
        item_schema = schema.get("items")
        if isinstance(item_schema, dict):
            for index, item in enumerate(value):
                self._validate_value(
                    item,
                    item_schema,
                    f"{path}[{index}]",
                    root_schema,
                    base_uri,
                    issues,
                    depth + 1,
                )

    @staticmethod
    def _validate_string(
        value: str,
        schema: dict[str, Any],
        path: str,
        issues: list[ValidationIssue],
    ) -> None:
        minimum = schema.get("minLength")
        maximum = schema.get("maxLength")
        if isinstance(minimum, int) and len(value) < minimum:
            issues.append(
                ValidationIssue(
                    "SCHEMA_MIN_LENGTH",
                    path,
                    f"String length {len(value)} is below {minimum}.",
                )
            )
        if isinstance(maximum, int) and len(value) > maximum:
            issues.append(
                ValidationIssue(
                    "SCHEMA_MAX_LENGTH",
                    path,
                    f"String length {len(value)} exceeds {maximum}.",
                )
            )
        pattern = schema.get("pattern")
        if isinstance(pattern, str) and re.search(pattern, value) is None:
            issues.append(
                ValidationIssue(
                    "SCHEMA_PATTERN", path, f"String does not match {pattern!r}."
                )
            )
        value_format = schema.get("format")
        try:
            if value_format == "uuid":
                uuid.UUID(value)
            elif value_format == "date-time":
                parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
                if parsed.tzinfo is None:
                    raise ValueError("timezone is required")
            elif value_format == "uri":
                parsed_uri = urlparse(value)
                if not parsed_uri.scheme:
                    raise ValueError("URI scheme is required")
        except (ValueError, AttributeError):
            issues.append(
                ValidationIssue(
                    "SCHEMA_FORMAT", path, f"String is not a valid {value_format}."
                )
            )

    @staticmethod
    def _validate_number(
        value: int | float,
        schema: dict[str, Any],
        path: str,
        issues: list[ValidationIssue],
    ) -> None:
        minimum = schema.get("minimum")
        maximum = schema.get("maximum")
        multiple = schema.get("multipleOf")
        if isinstance(minimum, (int, float)) and value < minimum:
            issues.append(
                ValidationIssue(
                    "SCHEMA_MINIMUM", path, f"Value {value} is below {minimum}."
                )
            )
        if isinstance(maximum, (int, float)) and value > maximum:
            issues.append(
                ValidationIssue(
                    "SCHEMA_MAXIMUM", path, f"Value {value} exceeds {maximum}."
                )
            )
        if isinstance(multiple, (int, float)) and multiple:
            quotient = value / multiple
            if abs(quotient - round(quotient)) > 1e-9:
                issues.append(
                    ValidationIssue(
                        "SCHEMA_MULTIPLE_OF",
                        path,
                        f"Value {value} is not a multiple of {multiple}.",
                    )
                )


def issues_as_dicts(issues: Iterable[ValidationIssue]) -> list[dict[str, str]]:
    return [issue.as_dict() for issue in issues]
