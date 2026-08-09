#!/usr/bin/env python3

# Copyright (C) 2026 Kvell

"""Validate and query the Embed Labs EtherCAT feature-to-source map."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import quote


FORMAT = "embed-labs-ethercat-feature-locator-v1"
FORMAT_VERSION = 1
MANIFEST_PATH = Path("docs/ethercat-feature-locator.json")
GENERATED_GUIDE_PATH = Path("docs/ethercat-feature-code-map.zh_CN.md")
SCHEMA_PATH = Path("docs/schemas/ethercat-feature-locator-v1.schema.json")
# The schema must exist before the first catalog commit can be staged. All
# feature source, contract, test, and documentation references still have to
# be tracked by Git.
BOOTSTRAP_UNTRACKED_PATHS = {SCHEMA_PATH.as_posix()}
RUNTIME_BOUNDARIES = {
    "contract-only",
    "engineering-only",
    "real-controller",
    "mixed-real-loopback",
    "loopback-only",
    "mock-only",
}
EVIDENCE_BOUNDARIES = {
    "unit",
    "offscreen-ui",
    "loopback",
    "artifact",
    "hardware-gated",
    "not-hardware-qualified",
}
ID_PATTERN = re.compile(r"^[a-z0-9]+(?:[.-][a-z0-9]+)*$")

ROOT_KEYS = {
    "$schema",
    "format",
    "formatVersion",
    "areas",
    "components",
    "features",
}
AREA_KEYS = {"id", "titleZh"}
COMPONENT_KEYS = {
    "id",
    "target",
    "directory",
    "cmake",
    "qbs",
    "metadata",
    "dependsOn",
    "responsibilityZh",
}
FEATURE_KEYS = {
    "id",
    "area",
    "titleZh",
    "summaryZh",
    "owner",
    "runtimeBoundary",
    "evidenceBoundary",
    "keywords",
    "implementation",
    "contracts",
    "tests",
    "docs",
    "dependsOn",
    "notesZh",
}
SOURCE_REFERENCE_KEYS = {"path", "symbols", "roleZh"}
TEST_REFERENCE_KEYS = {"path", "cases", "kind"}


class LocatorError(RuntimeError):
    pass


def repository_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise LocatorError(f"Missing file: {path}") from error
    except json.JSONDecodeError as error:
        raise LocatorError(f"Invalid JSON in {path}: {error}") from error
    except OSError as error:
        raise LocatorError(f"Cannot read {path}: {error}") from error


def load_manifest(root: Path) -> dict[str, Any]:
    data = load_json(root / MANIFEST_PATH)
    if not isinstance(data, dict):
        raise LocatorError("Feature locator root must be an object.")
    return data


def extra_keys(value: dict[str, Any], allowed: set[str]) -> set[str]:
    return set(value) - allowed


def relative_repository_path(root: Path, value: Any, label: str, errors: list[str]) -> Path | None:
    if not isinstance(value, str) or not value:
        errors.append(f"{label} must be a non-empty repository-relative path.")
        return None
    path = Path(value)
    if (
        path.is_absolute()
        or path == Path(".")
        or ".." in path.parts
        or "\\" in value
        or any(ord(character) < 32 for character in value)
        or value != path.as_posix()
    ):
        errors.append(f"{label} is not a normalized repository-relative path: {value}")
        return None
    candidate = root / path
    try:
        candidate.resolve(strict=False).relative_to(root.resolve())
    except (OSError, RuntimeError, ValueError):
        errors.append(f"{label} resolves outside the repository: {value}")
        return None
    if not candidate.exists():
        errors.append(f"{label} does not exist: {value}")
        return None
    return path


def tracked_paths(root: Path) -> set[str]:
    result = subprocess.run(
        ["git", "ls-files"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    return {line for line in result.stdout.splitlines() if line}


def is_tracked_or_bootstrap(path: Path, tracked: set[str]) -> bool:
    value = path.as_posix()
    catalog_is_new = MANIFEST_PATH.as_posix() not in tracked
    return value in tracked or (catalog_is_new and value in BOOTSTRAP_UNTRACKED_PATHS)


def validate_string_list(
    value: Any,
    label: str,
    errors: list[str],
    *,
    allow_empty: bool = False,
) -> list[str] | None:
    if not isinstance(value, list):
        errors.append(f"{label} must be an array of non-empty strings.")
        return None
    if not allow_empty and not value:
        errors.append(f"{label} must contain at least one non-empty string.")
        return None
    if not all(isinstance(item, str) and item.strip() == item and item for item in value):
        errors.append(f"{label} must contain only trimmed, non-empty strings.")
        return None
    if len(value) != len(set(value)):
        errors.append(f"{label} must not contain duplicates.")
        return None
    return value


def validate_schema_contract(root: Path, schema_path: Path, errors: list[str]) -> None:
    try:
        schema = load_json(root / schema_path)
    except LocatorError as error:
        errors.append(str(error))
        return
    if not isinstance(schema, dict):
        errors.append("Feature locator schema root must be an object.")
        return
    if schema.get("type") != "object":
        errors.append("Feature locator schema root type must be object.")
    if schema.get("additionalProperties") is not False:
        errors.append("Feature locator schema root must reject additional properties.")
    properties = schema.get("properties")
    if not isinstance(properties, dict) or set(properties) != ROOT_KEYS:
        actual = sorted(properties) if isinstance(properties, dict) else properties
        errors.append(
            "Feature locator schema root properties must match the validator keys: "
            f"expected {sorted(ROOT_KEYS)}, got {actual}."
        )
    else:
        def property_const(name: str) -> Any:
            property_schema = properties.get(name)
            return property_schema.get("const") if isinstance(property_schema, dict) else None

        if (
            property_const("$schema") != SCHEMA_PATH.as_posix()
            or property_const("format") != FORMAT
            or property_const("formatVersion") != FORMAT_VERSION
        ):
            errors.append("Feature locator schema constants do not match the validator constants.")
    required = schema.get("required")
    if (
        not isinstance(required, list)
        or not all(isinstance(item, str) for item in required)
        or len(required) != len(ROOT_KEYS)
        or set(required) != ROOT_KEYS
    ):
        actual = required
        errors.append(
            "Feature locator schema root required keys must match the validator keys: "
            f"expected {sorted(ROOT_KEYS)}, got {actual}."
        )
    definitions = schema.get("$defs")
    if not isinstance(definitions, dict):
        errors.append("Feature locator schema must define $defs.")
        return
    for name, expected in (
        ("runtimeBoundary", RUNTIME_BOUNDARIES),
        ("evidenceBoundary", EVIDENCE_BOUNDARIES),
    ):
        definition = definitions.get(name)
        actual = definition.get("enum") if isinstance(definition, dict) else None
        if (
            not isinstance(actual, list)
            or not all(isinstance(item, str) for item in actual)
            or len(actual) != len(expected)
            or set(actual) != expected
        ):
            errors.append(
                f"Feature locator schema {name} enum must match the validator: "
                f"expected {sorted(expected)}, got {actual}."
            )


def validate_symbols(
    root: Path,
    path: Path,
    symbols: Any,
    label: str,
    errors: list[str],
    *,
    field_name: str = "symbols",
) -> None:
    validated_symbols = validate_string_list(symbols, f"{label}.{field_name}", errors)
    if validated_symbols is None:
        return
    if not (root / path).is_file():
        errors.append(f"{label}.path must name a file: {path.as_posix()}")
        return
    contents = (root / path).read_text(encoding="utf-8", errors="replace")
    for symbol in validated_symbols:
        if symbol not in contents:
            errors.append(f"{label} symbol not found in {path.as_posix()}: {symbol}")


def validate_source_references(
    root: Path,
    tracked: set[str],
    references: Any,
    label: str,
    owner_directory: Path | None,
    errors: list[str],
) -> None:
    if not isinstance(references, list) or not references:
        errors.append(f"{label} must contain at least one source reference.")
        return
    for index, reference in enumerate(references):
        item_label = f"{label}[{index}]"
        if not isinstance(reference, dict):
            errors.append(f"{item_label} must be an object.")
            continue
        unknown = extra_keys(reference, SOURCE_REFERENCE_KEYS)
        if unknown:
            errors.append(f"{item_label} has unknown keys: {sorted(unknown)}")
        path = relative_repository_path(root, reference.get("path"), f"{item_label}.path", errors)
        if path is None:
            validate_string_list(reference.get("symbols"), f"{item_label}.symbols", errors)
        else:
            if path.as_posix() not in tracked:
                errors.append(f"{item_label}.path is not tracked by git: {path.as_posix()}")
            if owner_directory is not None and owner_directory not in path.parents:
                errors.append(
                    f"{item_label}.path is outside owner directory "
                    f"{owner_directory.as_posix()}: {path.as_posix()}"
                )
            validate_symbols(root, path, reference.get("symbols"), item_label, errors)
        role = reference.get("roleZh")
        if not isinstance(role, str) or not role.strip() or role.strip() != role:
            errors.append(f"{item_label}.roleZh must be a trimmed, non-empty string.")
    canonical_references = [
        json.dumps(reference, ensure_ascii=False, sort_keys=True)
        for reference in references
        if isinstance(reference, dict)
    ]
    if len(canonical_references) != len(set(canonical_references)):
        errors.append(f"{label} must not contain duplicate source references.")


def validate_tests(
    root: Path,
    tracked: set[str],
    tests: Any,
    label: str,
    errors: list[str],
) -> None:
    if not isinstance(tests, list) or not tests:
        errors.append(f"{label} must contain at least one test reference.")
        return
    for index, reference in enumerate(tests):
        item_label = f"{label}[{index}]"
        if not isinstance(reference, dict):
            errors.append(f"{item_label} must be an object.")
            continue
        unknown = extra_keys(reference, TEST_REFERENCE_KEYS)
        if unknown:
            errors.append(f"{item_label} has unknown keys: {sorted(unknown)}")
        path = relative_repository_path(root, reference.get("path"), f"{item_label}.path", errors)
        cases = reference.get("cases")
        if path is None:
            validate_string_list(cases, f"{item_label}.cases", errors)
        else:
            if path.as_posix() not in tracked:
                errors.append(f"{item_label}.path is not tracked by git: {path.as_posix()}")
            validate_symbols(root, path, cases, item_label, errors, field_name="cases")
        kind = reference.get("kind")
        if not isinstance(kind, str) or kind not in EVIDENCE_BOUNDARIES:
            errors.append(f"{item_label}.kind is invalid: {kind}")
    canonical_references = [
        json.dumps(reference, ensure_ascii=False, sort_keys=True)
        for reference in tests
        if isinstance(reference, dict)
    ]
    if len(canonical_references) != len(set(canonical_references)):
        errors.append(f"{label} must not contain duplicate test references.")


def validate_dependency_graph(
    nodes: Iterable[str],
    dependencies: dict[str, list[str]],
    label: str,
    errors: list[str],
) -> None:
    state: dict[str, int] = {node: 0 for node in nodes}

    def visit(node: str, stack: list[str]) -> None:
        if state[node] == 2:
            return
        if state[node] == 1:
            errors.append(f"{label} dependency cycle: {' -> '.join(stack + [node])}")
            return
        state[node] = 1
        for dependency in dependencies.get(node, []):
            if dependency in state:
                visit(dependency, stack + [node])
        state[node] = 2

    for node in state:
        visit(node, [])


def validate_manifest(root: Path, data: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    unknown_root = extra_keys(data, ROOT_KEYS)
    if unknown_root:
        errors.append(f"Manifest has unknown root keys: {sorted(unknown_root)}")
    if data.get("$schema") != SCHEMA_PATH.as_posix():
        errors.append(f"$schema must be {SCHEMA_PATH.as_posix()}")
    if data.get("format") != FORMAT:
        errors.append(f"format must be {FORMAT}")
    if data.get("formatVersion") != FORMAT_VERSION:
        errors.append(f"formatVersion must be {FORMAT_VERSION}")

    tracked = tracked_paths(root)
    schema = relative_repository_path(root, data.get("$schema"), "$schema", errors)
    if schema and not is_tracked_or_bootstrap(schema, tracked):
        errors.append(f"$schema is not tracked by git: {schema.as_posix()}")
    if schema:
        if not (root / schema).is_file():
            errors.append(f"$schema must name a file: {schema.as_posix()}")
        else:
            validate_schema_contract(root, schema, errors)

    areas = data.get("areas")
    area_ids: list[str] = []
    if not isinstance(areas, list) or not areas:
        errors.append("areas must be a non-empty array.")
    else:
        for index, area in enumerate(areas):
            label = f"areas[{index}]"
            if not isinstance(area, dict):
                errors.append(f"{label} must be an object.")
                continue
            unknown = extra_keys(area, AREA_KEYS)
            if unknown:
                errors.append(f"{label} has unknown keys: {sorted(unknown)}")
            area_id = area.get("id")
            if not isinstance(area_id, str) or not ID_PATTERN.fullmatch(area_id):
                errors.append(f"{label}.id is invalid: {area_id}")
            else:
                area_ids.append(area_id)
            title = area.get("titleZh")
            if not isinstance(title, str) or not title.strip() or title.strip() != title:
                errors.append(f"{label}.titleZh must be a trimmed, non-empty string.")
    if len(area_ids) != len(set(area_ids)):
        errors.append("Area IDs must be unique.")

    components = data.get("components")
    component_ids: list[str] = []
    component_targets: list[str] = []
    component_by_id: dict[str, dict[str, Any]] = {}
    component_dependencies: dict[str, list[str]] = {}
    component_directory_by_id: dict[str, Path] = {}
    component_build_paths: dict[str, dict[str, Path]] = {}
    component_directories: set[str] = set()
    if not isinstance(components, list) or not components:
        errors.append("components must be a non-empty array.")
    else:
        for index, component in enumerate(components):
            label = f"components[{index}]"
            if not isinstance(component, dict):
                errors.append(f"{label} must be an object.")
                continue
            unknown = extra_keys(component, COMPONENT_KEYS)
            if unknown:
                errors.append(f"{label} has unknown keys: {sorted(unknown)}")
            component_id = component.get("id")
            if not isinstance(component_id, str) or not ID_PATTERN.fullmatch(component_id):
                errors.append(f"{label}.id is invalid: {component_id}")
                continue
            component_ids.append(component_id)
            component_by_id[component_id] = component
            target = component.get("target")
            if not isinstance(target, str) or not target.strip() or target.strip() != target:
                errors.append(f"{label}.target must be a trimmed, non-empty string.")
            else:
                component_targets.append(target)
            responsibility = component.get("responsibilityZh")
            if (
                not isinstance(responsibility, str)
                or not responsibility.strip()
                or responsibility.strip() != responsibility
            ):
                errors.append(f"{label}.responsibilityZh must be a trimmed, non-empty string.")
            directory = relative_repository_path(
                root, component.get("directory"), f"{label}.directory", errors
            )
            if directory:
                if not (root / directory).is_dir():
                    errors.append(f"{label}.directory is not a directory: {directory.as_posix()}")
                else:
                    component_directory_by_id[component_id] = directory
                    component_directories.add(directory.as_posix())
            build_paths: dict[str, Path] = {}
            for key in ("cmake", "qbs"):
                path = relative_repository_path(root, component.get(key), f"{label}.{key}", errors)
                if path:
                    if not (root / path).is_file():
                        errors.append(f"{label}.{key} must name a file: {path.as_posix()}")
                    if directory and path.parent != directory:
                        errors.append(
                            f"{label}.{key} must be directly inside {directory.as_posix()}: "
                            f"{path.as_posix()}"
                        )
                    if path.as_posix() not in tracked:
                        errors.append(f"{label}.{key} is not tracked by git: {path.as_posix()}")
                    build_paths[key] = path
            component_build_paths[component_id] = build_paths
            metadata_value = component.get("metadata")
            if directory and directory.parts[:2] == ("src", "plugins") and metadata_value is None:
                errors.append(f"{label}.metadata is required for plugin components.")
            if metadata_value is not None:
                path = relative_repository_path(root, metadata_value, f"{label}.metadata", errors)
                if path:
                    if not (root / path).is_file():
                        errors.append(f"{label}.metadata must name a file: {path.as_posix()}")
                    if directory and path.parent != directory:
                        errors.append(
                            f"{label}.metadata must be directly inside {directory.as_posix()}: "
                            f"{path.as_posix()}"
                        )
                    if path.as_posix() not in tracked:
                        errors.append(f"{label}.metadata is not tracked by git: {path.as_posix()}")
            dependencies = validate_string_list(
                component.get("dependsOn"), f"{label}.dependsOn", errors, allow_empty=True
            )
            if dependencies is None:
                dependencies = []
            component_dependencies[component_id] = dependencies
    if len(component_ids) != len(set(component_ids)):
        errors.append("Component IDs must be unique.")
    if len(component_targets) != len(set(component_targets)):
        errors.append("Component targets must be unique.")
    for component_id, dependencies in component_dependencies.items():
        for dependency in dependencies:
            if dependency not in component_by_id:
                errors.append(
                    f"Component {component_id} depends on unknown component {dependency}."
                )
            if dependency == component_id:
                errors.append(f"Component {component_id} cannot depend on itself.")
    validate_dependency_graph(component_ids, component_dependencies, "Component", errors)

    for component_id, dependencies in component_dependencies.items():
        component = component_by_id.get(component_id)
        build_paths = component_build_paths.get(component_id, {})
        if component is None or not isinstance(component.get("target"), str):
            continue
        expected_targets = [component["target"]]
        expected_targets.extend(
            component_by_id[dependency]["target"]
            for dependency in dependencies
            if dependency in component_by_id
            and isinstance(component_by_id[dependency].get("target"), str)
        )
        for kind, build_path in build_paths.items():
            if not (root / build_path).is_file():
                continue
            contents = (root / build_path).read_text(encoding="utf-8", errors="replace")
            for target in expected_targets:
                if target not in contents:
                    errors.append(
                        f"Component {component_id} target {target} is missing from "
                        f"{kind} descriptor {build_path.as_posix()}."
                    )

    expected_directories = {
        path.relative_to(root).as_posix()
        for path in (root / "src/plugins").glob("ethercat*")
        if path.is_dir() and (path / "CMakeLists.txt").is_file()
    }
    expected_directories.add("src/libs/ethercatdata")
    if component_directories != expected_directories:
        missing = sorted(expected_directories - component_directories)
        extra = sorted(component_directories - expected_directories)
        if missing:
            errors.append(f"Component catalog is missing directories: {missing}")
        if extra:
            errors.append(f"Component catalog has unexpected directories: {extra}")

    features = data.get("features")
    feature_ids: list[str] = []
    feature_by_id: dict[str, dict[str, Any]] = {}
    feature_dependencies: dict[str, list[str]] = {}
    owner_counts = {component_id: 0 for component_id in component_ids}
    if not isinstance(features, list) or not features:
        errors.append("features must be a non-empty array.")
    else:
        for index, feature in enumerate(features):
            label = f"features[{index}]"
            if not isinstance(feature, dict):
                errors.append(f"{label} must be an object.")
                continue
            unknown = extra_keys(feature, FEATURE_KEYS)
            if unknown:
                errors.append(f"{label} has unknown keys: {sorted(unknown)}")
            feature_id = feature.get("id")
            if not isinstance(feature_id, str) or not ID_PATTERN.fullmatch(feature_id):
                errors.append(f"{label}.id is invalid: {feature_id}")
                continue
            feature_ids.append(feature_id)
            feature_by_id[feature_id] = feature
            for key in ("titleZh", "summaryZh"):
                value = feature.get(key)
                if not isinstance(value, str) or not value.strip() or value.strip() != value:
                    errors.append(f"{label}.{key} must be a trimmed, non-empty string.")
            if feature.get("area") not in area_ids:
                errors.append(f"{label}.area is unknown: {feature.get('area')}")
            owner = feature.get("owner")
            if not isinstance(owner, str) or owner not in component_by_id:
                errors.append(f"{label}.owner is unknown: {owner}")
                owner_directory = None
            else:
                owner_counts[owner] += 1
                owner_directory = component_directory_by_id.get(owner)
            runtime_boundary = feature.get("runtimeBoundary")
            if not isinstance(runtime_boundary, str) or runtime_boundary not in RUNTIME_BOUNDARIES:
                errors.append(
                    f"{label}.runtimeBoundary is invalid: "
                    f"{runtime_boundary}"
                )
            evidence = feature.get("evidenceBoundary")
            validated_evidence = validate_string_list(
                evidence, f"{label}.evidenceBoundary", errors
            )
            if (
                validated_evidence is not None
                and not set(validated_evidence) <= EVIDENCE_BOUNDARIES
            ):
                errors.append(f"{label}.evidenceBoundary contains invalid values: {evidence}")
            validate_string_list(feature.get("keywords"), f"{label}.keywords", errors)
            validate_source_references(
                root,
                tracked,
                feature.get("implementation"),
                f"{label}.implementation",
                owner_directory,
                errors,
            )
            validate_source_references(
                root, tracked, feature.get("contracts"), f"{label}.contracts", None, errors
            )
            validate_tests(root, tracked, feature.get("tests"), f"{label}.tests", errors)
            docs = validate_string_list(feature.get("docs"), f"{label}.docs", errors)
            if docs is not None:
                for doc_index, doc_value in enumerate(docs):
                    path = relative_repository_path(
                        root, doc_value, f"{label}.docs[{doc_index}]", errors
                    )
                    if path:
                        if not (root / path).is_file():
                            errors.append(
                                f"{label}.docs[{doc_index}] must name a file: {path.as_posix()}"
                            )
                        if path.as_posix() not in tracked:
                            errors.append(
                                f"{label}.docs[{doc_index}] is not tracked by git: "
                                f"{path.as_posix()}"
                            )
            dependencies = validate_string_list(
                feature.get("dependsOn"), f"{label}.dependsOn", errors, allow_empty=True
            )
            if dependencies is None:
                dependencies = []
            feature_dependencies[feature_id] = dependencies
            validate_string_list(
                feature.get("notesZh"), f"{label}.notesZh", errors, allow_empty=True
            )
    if len(feature_ids) != len(set(feature_ids)):
        errors.append("Feature IDs must be unique.")
    for feature_id, dependencies in feature_dependencies.items():
        for dependency in dependencies:
            if dependency not in feature_by_id:
                errors.append(f"Feature {feature_id} depends on unknown feature {dependency}.")
            if dependency == feature_id:
                errors.append(f"Feature {feature_id} cannot depend on itself.")
    validate_dependency_graph(feature_ids, feature_dependencies, "Feature", errors)
    unowned = sorted(component_id for component_id, count in owner_counts.items() if count == 0)
    if unowned:
        errors.append(f"Every component must own at least one feature; missing: {unowned}")
    used_areas = {
        feature.get("area")
        for feature in feature_by_id.values()
        if isinstance(feature.get("area"), str)
    }
    unused_areas = sorted(set(area_ids) - used_areas)
    if unused_areas:
        errors.append(f"Every area must contain at least one feature; missing: {unused_areas}")

    return errors


def markdown_link(path: str) -> str:
    target = quote(f"../{path}", safe="/._-~")
    return f"[`{path}`]({target})"


def markdown_table_text(value: str) -> str:
    return value.replace("|", "\\|").replace("\r", " ").replace("\n", " ")


def render_markdown(data: dict[str, Any]) -> str:
    components = {component["id"]: component for component in data["components"]}
    lines = [
        "# Embed Labs EtherCAT 功能与源码定位地图",
        "",
        "> 本文件由 `docs/ethercat-feature-locator.json` 确定性生成。",
        "> 不要直接编辑；先修改机器清单，再运行：",
        "> `python3 scripts/ethercat_feature_locator.py generate`",
        "",
        "## 1. 不搜索源码的使用方法",
        "",
        "```sh",
        "python3 scripts/ethercat_feature_locator.py list",
        "python3 scripts/ethercat_feature_locator.py find 扫描",
        "python3 scripts/ethercat_feature_locator.py show ethercat.product-api.topology-evidence",
        "python3 scripts/ethercat_feature_locator.py check",
        "```",
        "",
        (
            "`find` 只查询已审校的功能清单；`show` 直接返回负责插件、"
            "入口符号、"
        ),
        "公共合同、测试和文档。新增或移动文件后，`check` 会拒绝不存在、",
        "未跟踪或符号已消失的映射。",
        "",
        "## 2. 组件和依赖方向",
        "",
        "| 组件 ID | Target | 目录 | 直接依赖 | 职责 |",
        "|---|---|---|---|---|",
    ]
    for component in data["components"]:
        dependencies = "、".join(f"`{item}`" for item in component["dependsOn"]) or "—"
        lines.append(
            f"| `{component['id']}` | `{component['target']}` | "
            f"{markdown_link(component['directory'])} | {dependencies} | "
            f"{markdown_table_text(component['responsibilityZh'])} |"
        )
    lines.extend(
        [
            "",
            "## 3. 功能快速索引",
            "",
            "| 功能 ID | 功能 | Owner | 边界 | 第一入口 |",
            "|---|---|---|---|---|",
        ]
    )
    for feature in data["features"]:
        first_path = feature["implementation"][0]["path"]
        lines.append(
            f"| `{feature['id']}` | {markdown_table_text(feature['titleZh'])} | "
            f"`{components[feature['owner']]['target']}` | `{feature['runtimeBoundary']}` | "
            f"{markdown_link(first_path)} |"
        )
    lines.extend(["", "## 4. 按领域查看修改入口", ""])
    rendered_area_index = 0
    for area in data["areas"]:
        area_features = [feature for feature in data["features"] if feature["area"] == area["id"]]
        if not area_features:
            continue
        rendered_area_index += 1
        lines.extend([f"### 4.{rendered_area_index} {area['titleZh']}", ""])
        for feature in area_features:
            owner = components[feature["owner"]]
            lines.extend(
                [
                    f"#### `{feature['id']}` — {feature['titleZh']}",
                    "",
                    feature["summaryZh"],
                    "",
                    f"- Owner：`{owner['target']}`（{markdown_link(owner['directory'])}）",
                    f"- 运行边界：`{feature['runtimeBoundary']}`",
                    "- 证据边界："
                    + "、".join(f"`{item}`" for item in feature["evidenceBoundary"]),
                    "- 修改入口：",
                ]
            )
            for reference in feature["implementation"]:
                symbols = "、".join(f"`{symbol}`" for symbol in reference["symbols"])
                lines.append(
                    f"  - {markdown_link(reference['path'])}："
                    f"{reference['roleZh']}；{symbols}"
                )
            lines.append("- 公共合同：")
            for reference in feature["contracts"]:
                symbols = "、".join(f"`{symbol}`" for symbol in reference["symbols"])
                lines.append(
                    f"  - {markdown_link(reference['path'])}："
                    f"{reference['roleZh']}；{symbols}"
                )
            lines.append("- 定向测试：")
            for reference in feature["tests"]:
                cases = "、".join(f"`{case}`" for case in reference["cases"])
                lines.append(
                    f"  - {markdown_link(reference['path'])}（`{reference['kind']}`）：{cases}"
                )
            lines.append(
                "- 相关文档："
                + "、".join(markdown_link(path) for path in feature["docs"])
            )
            if feature["dependsOn"]:
                lines.append(
                    "- 前置功能：" + "、".join(f"`{item}`" for item in feature["dependsOn"])
                )
            for note in feature["notesZh"]:
                lines.append(f"- 边界提醒：{note}")
            lines.append("")
    lines.extend(
        [
            "## 5. 修改前的最短决策",
            "",
            "- 改工程字段或保存格式：从 `ethercat.project.model-format` 开始。",
            "- 加新厂家/型号：从 `ethercat.devices.esi-repository` 和",
            "  `ethercat.adapters.catalog-authorization` 开始，不改 Workbench/Product API。",
            "- 改连接、扫描、状态或协议：从 `ethercat.product-api.*` 开始。",
            "- 改页面或按钮：从 `ethercat.workbench.*` 开始，",
            "  只调用 Core 公共服务。",
            "- 改手动控制：先看 `ethercat.runtime.binding-actions`，再看",
            "  `ethercat.runtime.manual-control` 和 `ethercat.product-api.output-transactions`。",
            "- 改编译/签名/部署：依次看 `ethercat.compiler.*`、",
            "  `ethercat.runtime.package-evidence`、`ethercat.runtime.activation`。",
            "- Mock Scan/Diagnostics 不能作为真实控制器入口；",
            "  以对应条目的边界字段为准。",
            "",
        ]
    )
    return "\n".join(lines)


def select_features(data: dict[str, Any], query: str) -> list[dict[str, Any]]:
    needle = query.strip().casefold()
    if not needle:
        raise LocatorError("Search query must not be empty.")
    return [
        feature
        for feature in data["features"]
        if needle in json.dumps(feature, ensure_ascii=False, sort_keys=True).casefold()
    ]


def print_feature(data: dict[str, Any], feature: dict[str, Any]) -> None:
    component = next(item for item in data["components"] if item["id"] == feature["owner"])
    print(f"{feature['id']}  {feature['titleZh']}")
    print(f"Owner: {component['target']}  Boundary: {feature['runtimeBoundary']}")
    print(feature["summaryZh"])
    print("Implementation:")
    for reference in feature["implementation"]:
        print(f"  {reference['path']} :: {', '.join(reference['symbols'])}")
    print("Contracts:")
    for reference in feature["contracts"]:
        print(f"  {reference['path']} :: {', '.join(reference['symbols'])}")
    print("Tests:")
    for reference in feature["tests"]:
        print(f"  {reference['path']} :: {', '.join(reference['cases'])} [{reference['kind']}]")
    print("Docs:")
    for path in feature["docs"]:
        print(f"  {path}")
    for note in feature["notesZh"]:
        print(f"Boundary note: {note}")


def ensure_valid(root: Path, data: dict[str, Any]) -> None:
    errors = validate_manifest(root, data)
    if errors:
        raise LocatorError("\n".join(f"- {error}" for error in errors))


def command_check(root: Path, data: dict[str, Any]) -> None:
    ensure_valid(root, data)
    generated_path = root / GENERATED_GUIDE_PATH
    expected = render_markdown(data)
    if not generated_path.is_file():
        raise LocatorError(f"Missing generated guide: {GENERATED_GUIDE_PATH.as_posix()}")
    actual = generated_path.read_text(encoding="utf-8")
    if actual != expected:
        raise LocatorError(
            "Generated guide is stale: run "
            "'python3 scripts/ethercat_feature_locator.py generate'."
        )
    print(
        f"PASS: {len(data['components'])} components, {len(data['features'])} features, "
        "all paths and symbols verified."
    )


def command_generate(root: Path, data: dict[str, Any]) -> None:
    ensure_valid(root, data)
    output = root / GENERATED_GUIDE_PATH
    rendered = render_markdown(data)
    if output.is_file() and output.read_text(encoding="utf-8") == rendered:
        print(f"Unchanged {GENERATED_GUIDE_PATH.as_posix()}")
        return
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="\n",
            dir=output.parent,
            prefix=f".{output.name}.",
            suffix=".tmp",
            delete=False,
        ) as temporary:
            temporary.write(rendered)
            temporary_path = Path(temporary.name)
        temporary_path.chmod(0o644)
        temporary_path.replace(output)
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
    print(f"Generated {GENERATED_GUIDE_PATH.as_posix()}")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check", help="validate the manifest and generated guide")
    subparsers.add_parser("generate", help="regenerate the human-readable guide")
    list_parser = subparsers.add_parser("list", help="list all stable feature IDs")
    list_parser.add_argument("--json", action="store_true", help="emit JSON")
    find_parser = subparsers.add_parser("find", help="find features without searching source")
    find_parser.add_argument("query")
    find_parser.add_argument("--json", action="store_true", help="emit JSON")
    show_parser = subparsers.add_parser("show", help="show one feature and its source entrypoints")
    show_parser.add_argument("feature_id")
    show_parser.add_argument("--json", action="store_true", help="emit JSON")
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    root = repository_root()
    try:
        data = load_manifest(root)
        if args.command == "check":
            command_check(root, data)
        elif args.command == "generate":
            command_generate(root, data)
        else:
            ensure_valid(root, data)
            if args.command == "list":
                features = data["features"]
            elif args.command == "find":
                features = select_features(data, args.query)
                if not features:
                    raise LocatorError(f"No feature matches: {args.query}")
            else:
                features = [
                    feature for feature in data["features"] if feature["id"] == args.feature_id
                ]
                if not features:
                    raise LocatorError(f"Unknown feature ID: {args.feature_id}")
            if args.json:
                payload = features if args.command != "show" else features[0]
                print(json.dumps(payload, ensure_ascii=False, indent=2))
            elif args.command == "show":
                print_feature(data, features[0])
            else:
                for feature in features:
                    print(f"{feature['id']:<48} {feature['titleZh']}")
        return 0
    except (LocatorError, OSError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
