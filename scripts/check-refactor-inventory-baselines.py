#!/usr/bin/env python3
"""Check compact architecture review evidence and current-format inventories."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import subprocess
import sys
import textwrap
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASELINE_PATH = ROOT / "tests" / "architecture" / "refactor_inventory_baseline.json"

PUBLIC_SNAPSHOTS = {
    "Board": ROOT / "tests" / "architecture" / "board_public_api.txt",
    "BoardRouter": ROOT / "tests" / "architecture" / "boardrouter_public_api.txt",
    "BoardSpatialIndex": ROOT
    / "tests"
    / "architecture"
    / "boardspatialindex_public_api.txt",
    "Circuit": ROOT / "tests" / "architecture" / "circuit_public_api.txt",
    "ComponentAssemblyIntent": ROOT
    / "tests"
    / "architecture"
    / "component_assembly_intent_public_api.txt",
    "NetClass": ROOT / "tests" / "architecture" / "net_class_public_api.txt",
    "Schematic": ROOT / "tests" / "architecture" / "schematic_public_api.txt",
}

SEMANTIC_GOLDEN_PATHS = (
    "tests/fixtures/semantic_parity.volt.json",
    "tests/fixtures/ap1117.part.volt.json",
    "tests/fixtures/regulator.electrical.volt.json",
    "tests/fixtures/mcu.electrical.volt.json",
    "tests/fixtures/led.electrical.volt.json",
    "tests/fixtures/led.component-contract.volt.json",
    "tests/fixtures/led_circuit.volt.json",
    "tests/fixtures/hierarchy_module.volt.json",
    "tests/fixtures/typed_electrical_attributes.volt.json",
    "tests/fixtures/single_pin_net.volt.json",
)

BYTE_GOLDEN_PATHS = (
    "tests/fixtures/ap1117.part.volt.json",
    "tests/fixtures/regulator.electrical.volt.json",
    "tests/fixtures/mcu.electrical.volt.json",
    "tests/fixtures/led.electrical.volt.json",
    "tests/fixtures/led.component-contract.volt.json",
    "tests/fixtures/native_fabrication_control.GBL",
    "tests/fixtures/native_fabrication_control.GBS",
    "tests/fixtures/native_fabrication_control.GKO",
    "tests/fixtures/native_fabrication_control.GTL",
    "tests/fixtures/native_fabrication_control.GTO",
    "tests/fixtures/native_fabrication_control.GTP",
    "tests/fixtures/native_fabrication_control.GTS",
    "tests/fixtures/native_fabrication_control-NPTH.TXT",
    "tests/fixtures/native_fabrication_control-PTH.TXT",
    "tests/fixtures/kicad_flat_resistor.kicad_pcb",
    "tests/fixtures/kicad_flat_resistor.kicad_sch",
    "tests/fixtures/pcb_placement_preview.svg",
    "tests/fixtures/semantic_parity.volt.json",
    "tests/fixtures/led_circuit.volt.json",
)

EVIDENCE_LF_PATHS = tuple(
    dict.fromkeys(
        [path.relative_to(ROOT).as_posix() for path in PUBLIC_SNAPSHOTS.values()]
        + list(SEMANTIC_GOLDEN_PATHS)
        + list(BYTE_GOLDEN_PATHS)
    )
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def sha256_bytes(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_check_attr_eol(output: bytes) -> dict[str, str]:
    fields = output.split(b"\0")
    if fields[-1] == b"":
        fields.pop()
    if len(fields) % 3:
        raise AssertionError("git check-attr returned malformed NUL-delimited output")
    return {
        fields[index].decode("utf-8"): fields[index + 2].decode("utf-8")
        for index in range(0, len(fields), 3)
        if fields[index + 1] == b"eol"
    }


def require_lf_checkout_paths() -> None:
    result = subprocess.run(
        ["git", "-C", str(ROOT), "check-attr", "-z", "eol", "--", *EVIDENCE_LF_PATHS],
        capture_output=True,
        check=False,
    )
    if result.returncode:
        raise AssertionError(
            f"git check-attr failed: {result.stderr.decode('utf-8', errors='replace')}"
        )
    attributes = parse_check_attr_eol(result.stdout)
    missing = [path for path in EVIDENCE_LF_PATHS if attributes.get(path) != "lf"]
    if missing:
        raise AssertionError(
            "byte-hashed evidence paths must be checked out with LF line endings: "
            + ", ".join(missing)
        )


def snapshot_summary(path: Path) -> dict[str, str]:
    return {"path": relative(path), "sha256": sha256_bytes(path)}


def python_all_exports() -> list[str]:
    tree = ast.parse(read(ROOT / "python" / "volt" / "__init__.py"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(
            isinstance(target, ast.Name) and target.id == "__all__"
            for target in node.targets
        ):
            continue
        return sorted(str(item) for item in ast.literal_eval(node.value))
    raise AssertionError("__all__ not found in python/volt/__init__.py")


def init_assigned_attributes(path: Path, class_name: str) -> list[str]:
    tree = ast.parse(read(path), filename=relative(path))
    for node in tree.body:
        if not isinstance(node, ast.ClassDef) or node.name != class_name:
            continue
        for child in node.body:
            if not isinstance(child, ast.FunctionDef) or child.name != "__init__":
                continue
            return sorted(
                {
                    subnode.attr
                    for subnode in ast.walk(child)
                    if isinstance(subnode, ast.Attribute)
                    and isinstance(subnode.value, ast.Name)
                    and subnode.value.id == "self"
                }
            )
    return []


def binding_private_members(path: str) -> list[str]:
    header = read(ROOT / path)
    private = header.split("  private:", 1)[1]
    return sorted(
        name
        for name in re.findall(r"\b([A-Za-z_]\w*)_\s*(?:=|;)", private)
        if name not in {"std", "volt"}
    )


def native_bound_classes() -> list[dict[str, str]]:
    result: list[dict[str, str]] = []
    pattern = re.compile(r'py::class_<([^>]+)>\(module,\s*"([^"]+)"\)')
    for path in sorted((ROOT / "src" / "python").glob("*_bindings.cpp")):
        for cpp_type, python_type in pattern.findall(read(path)):
            result.append(
                {
                    "cpp_type": cpp_type,
                    "python_type": python_type,
                    "path": relative(path),
                }
            )
    return result


def python_exception_inventory() -> dict[str, object]:
    source = read(ROOT / "src" / "python" / "bindings.cpp")
    return {
        "exception_classes": re.findall(
            r'create_exception_class\(\s*module,\s*"([^"]+)"', source
        ),
        "exception_metadata_keys": sorted(
            set(re.findall(r'exception\.attr\("([^"]+)"\)', source))
        ),
        "entity_metadata_keys": sorted(re.findall(r'result\["([^"]+)"\]', source)),
    }


def current_cpp_function(text: str, position: int) -> str:
    prefix = text[:position]
    matches = list(
        re.finditer(
            r"\b(?:[A-Za-z_:<>~*&\s]+)\s+(Py(?:Circuit|Board)::[A-Za-z_]\w*)\s*"
            r"\([^;{}]*\)\s*(?:const\s*)?\{",
            prefix,
        )
    )
    return matches[-1].group(1) if matches else "<unknown>"


def hidden_footprint_injections() -> list[dict[str, str]]:
    injections: list[dict[str, str]] = []
    pattern = re.compile(r"volt::builtin_footprint_library\s*\(\)")
    for path in sorted((ROOT / "src" / "python").glob("*.cpp")):
        text = read(path)
        for match in pattern.finditer(text):
            injections.append(
                {
                    "path": relative(path),
                    "caller": current_cpp_function(text, match.start()),
                }
            )
    return injections


def qualified_python_name(node: ast.expr) -> str | None:
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        owner = qualified_python_name(node.value)
        return node.attr if owner is None else f"{owner}.{node.attr}"
    return None


def current_project_writer_ownership(project_source: str) -> dict[str, object]:
    tree = ast.parse(project_source, filename="python/volt/project.py")
    project_result = next(
        (
            node
            for node in tree.body
            if isinstance(node, ast.ClassDef) and node.name == "ProjectResult"
        ),
        None,
    )
    require(project_result is not None, "ProjectResult must remain the current Project owner")
    write_method = next(
        (
            node
            for node in project_result.body
            if isinstance(node, ast.FunctionDef) and node.name == "write"
        ),
        None,
    )
    require(write_method is not None, "ProjectResult.write must remain the current write entrypoint")
    selected_write_method = next(
        (
            node
            for node in project_result.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "_write_selected_bundle"
        ),
        None,
    )
    require(
        selected_write_method is not None,
        "ProjectResult must retain one private selected-export publication helper",
    )

    write_calls = [
        node for node in ast.walk(write_method) if isinstance(node, ast.Call)
    ]
    write_call_names = [
        qualified_python_name(call.func) for call in write_calls
    ]
    require(
        write_call_names == ["self._write_selected_bundle"],
        "ProjectResult.write must be a thin delegation to the selected-export helper",
    )
    delegation = write_calls[0]
    require(
        any(
            isinstance(statement, ast.Expr) and statement.value is delegation
            for statement in write_method.body
        ),
        "ProjectResult.write must invoke the selected-export helper directly",
    )
    require(
        len(delegation.args) == 1
        and isinstance(delegation.args[0], ast.Name)
        and delegation.args[0].id == "path",
        "ProjectResult.write must forward its requested destination unchanged",
    )
    profile_keywords = [
        keyword for keyword in delegation.keywords if keyword.arg == "profile"
    ]
    require(
        len(profile_keywords) == 1
        and isinstance(profile_keywords[0].value, ast.Name)
        and profile_keywords[0].value.id == "profile",
        "ProjectResult.write must forward its requested profile unchanged",
    )
    selected_exports_keywords = [
        keyword
        for keyword in delegation.keywords
        if keyword.arg == "selected_exports"
    ]
    require(
        len(selected_exports_keywords) == 1
        and isinstance(selected_exports_keywords[0].value, ast.List)
        and not selected_exports_keywords[0].value.elts,
        "ProjectResult.write must request an empty selected-export closure",
    )

    calls = [
        node for node in ast.walk(selected_write_method) if isinstance(node, ast.Call)
    ]
    call_names = [qualified_python_name(call.func) for call in calls]
    native_calls = [
        call
        for call, name in zip(calls, call_names)
        if name == "_volt._write_project_bundle_v2"
    ]
    require(
        len(native_calls) == 1,
        "ProjectResult selected-export helper must call the native typed v2 writer exactly once",
    )
    native_call = native_calls[0]
    require(
        any(
            isinstance(statement, ast.Return) and statement.value is native_call
            for statement in selected_write_method.body
        ),
        "ProjectResult selected-export helper must return the native typed v2 writer directly",
    )
    all_native_calls = [
        node
        for node in ast.walk(project_result)
        if isinstance(node, ast.Call)
        and qualified_python_name(node.func) == "_volt._write_project_bundle_v2"
    ]
    require(
        len(all_native_calls) == 1,
        "ProjectResult must have exactly one native typed v2 publication call",
    )
    board_preparation_calls = [
        call
        for call, name in zip(calls, call_names)
        if name == "_volt._prepare_project_bundle_board"
    ]
    require(
        len(board_preparation_calls) == 1,
        "ProjectResult selected-export helper must prepare complete native Board artifacts "
        "exactly once",
    )
    require(
        board_preparation_calls[0] in set(ast.walk(native_call)),
        "native Board artifacts must flow directly into the native v2 writer",
    )

    allowed_calls = {
        "Path",
        "ValueError",
        "_bundle_policy_snapshot",
        "_canonical_report_bytes",
        "_diagnostics_payload",
        "_project_authoring_inputs",
        "_tests_payload",
        "_volt._prepare_project_bundle_board",
        "_volt._write_project_bundle_v2",
        "str",
    }
    unexpected_calls = sorted(
        name for name in call_names if name is None or name not in allowed_calls
    )
    require(
        not unexpected_calls,
        "ProjectResult selected-export helper may only prepare typed native inputs before "
        "publication; "
        f"unexpected calls: {unexpected_calls}",
    )

    native_nodes = set(ast.walk(native_call))
    path_uses = [
        node
        for node in ast.walk(selected_write_method)
        if isinstance(node, ast.Name) and node.id == "path"
    ]
    require(
        path_uses,
        "ProjectResult selected-export helper must consume its requested destination",
    )
    require(
        all(node in native_nodes for node in path_uses),
        "ProjectResult selected-export helper may pass its destination only to the native "
        "typed v2 writer",
    )

    retired_definitions = [
        node
        for node in ast.walk(tree)
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == "_artifact_record"
    ]
    retired_calls = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Call)
        and qualified_python_name(node.func) in {"_artifact_record", "self._artifact_record"}
    ]
    require(
        not retired_definitions and not retired_calls,
        "the retired Python artifact-record writer must not exist in the current Project path",
    )

    return {
        "entrypoint": "ProjectResult.write",
        "retired_python_artifact_record": False,
        "native_board_preparation": "_volt._prepare_project_bundle_board",
        "native_binding": "_volt._write_project_bundle_v2",
    }


def native_project_artifact_kinds() -> list[str]:
    source = read(ROOT / "src" / "io" / "project_bundle_v2_manifest.cpp")
    return sorted(
        set(
            re.findall(
                r'std::pair\{"([a-z0-9_]+)",\s*ArtifactKind::[A-Za-z0-9_]+\}',
                source,
            )
        )
    )


def canonical_artifact_inventory() -> dict[str, object]:
    return {
        "current_writer_ownership": current_project_writer_ownership(
            read(ROOT / "python" / "volt" / "project.py"),
        ),
        "native_project_artifact_kinds": native_project_artifact_kinds(),
    }


def offline_fixtures() -> list[str]:
    return sorted(
        relative(path)
        for path in (ROOT / "tests" / "fixtures").glob("*")
        if path.name.endswith(".part.volt.json")
        or path.name.endswith(".electrical.volt.json")
        or path.name.endswith(".voltcap.json")
        or path.name.startswith("native_fabrication_control")
        or path.name.startswith("kicad_flat_resistor")
    )


def tracked_paths() -> list[str]:
    result = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "-z"],
        capture_output=True,
        check=False,
    )
    if result.returncode:
        raise AssertionError(
            f"git ls-files failed: {result.stderr.decode('utf-8', errors='replace')}"
        )
    paths = sorted(
        path
        for field in result.stdout.split(b"\0")
        if field
        for path in (field.decode("utf-8"),)
    )
    require_tracked_paths_exist(paths)
    return paths


def require_tracked_paths_exist(paths: list[str]) -> None:
    missing = [path for path in paths if not (ROOT / path).exists()]
    if missing:
        raise AssertionError(
            "git index contains tracked paths missing from the worktree: " + ", ".join(missing)
        )


ANTI_REGROWTH_PATH_PREFIXES = (
    "examples/",
    "python/tests/examples/",
    "python/volt/libraries/",
)

ANTI_REGROWTH_TEXT_PATTERNS = {
    "retired_example_reference": re.compile(r"(?<![A-Za-z0-9_])examples/"),
    "retired_stm32_benchmark": re.compile(r"\bstm32_usb_buck\b"),
    "retired_library_component": re.compile(r"\bLibraryComponent\b"),
    "retired_physical_part_spec": re.compile(r"\bPhysicalPartSpec\b"),
    "retired_part_definition_lowering": re.compile(r"\b_PartDefinition\b"),
    "retired_instance_selection": re.compile(r"\.select_part\s*\("),
    "retired_flat_writer": re.compile(r"\bwrite_artifacts\s*\("),
    "retired_flat_writer_paths": re.compile(r"\bProjectArtifactPaths\b"),
    "retired_flat_cli": re.compile(r'"--flat"|`volt build --flat`'),
    "retired_source_cli_alias": re.compile(r"\bvolt (?:run|model|diagnostics|info)\b"),
    "retired_source_manufacturing_cli": re.compile(
        r"\bvolt export manufacturing\b|source-backed-export-retired|"
        r"\b_handle_export_manufacturing\b"
    ),
    "retired_projection_forwarding": re.compile(r"\bselect_authored_part\b"),
}

ANTI_REGROWTH_SCAN_PREFIXES = (
    "docs/",
    "docs-site/",
    "python/",
    "skills/",
    "src/",
)

ANTI_REGROWTH_SCAN_FILES = {
    ".gitattributes",
    "CMakeLists.txt",
    "README.md",
    "pyproject.toml",
}

def retired_example_import_lines(path: str, source: str) -> set[int]:
    if not path.endswith(".py"):
        return set()
    try:
        tree = ast.parse(source)
    except SyntaxError as error:
        raise AssertionError(f"cannot inspect Python imports in {path}: {error}") from error
    lines: set[int] = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            if any(
                alias.name == "examples" or alias.name.startswith("examples.")
                for alias in node.names
            ):
                lines.add(node.lineno)
        elif isinstance(node, ast.ImportFrom) and node.module is not None:
            if node.module == "examples" or node.module.startswith("examples."):
                lines.add(node.lineno)
    return lines


def anti_regrowth_text_violations(sources: dict[str, str]) -> list[str]:
    violations: list[str] = []
    for path, source in sorted(sources.items()):
        example_import_lines = retired_example_import_lines(path, source)
        for line_number, line in enumerate(source.splitlines(), start=1):
            if line_number in example_import_lines:
                violations.append(f"retired_example_reference:{path}:{line_number}")
            for name, pattern in ANTI_REGROWTH_TEXT_PATTERNS.items():
                violation = f"{name}:{path}:{line_number}"
                if pattern.search(line) and violation not in violations:
                    violations.append(violation)
    return violations


def anti_regrowth_inventory(paths: list[str]) -> dict[str, object]:
    forbidden_paths = sorted(
        path
        for path in paths
        if any(path.startswith(prefix) for prefix in ANTI_REGROWTH_PATH_PREFIXES)
    )
    sources: dict[str, str] = {}
    for path in paths:
        if not path.startswith(ANTI_REGROWTH_SCAN_PREFIXES) and path not in ANTI_REGROWTH_SCAN_FILES:
            continue
        try:
            sources[path] = read(ROOT / path)
        except (UnicodeDecodeError, OSError):
            continue
    return {
        "forbidden_paths": forbidden_paths,
        "retired_route_references": anti_regrowth_text_violations(sources),
    }


def bound_component_definition_routes() -> list[str]:
    source = read(ROOT / "src" / "python" / "circuit_bindings.cpp")
    return sorted(
        route
        for route in re.findall(r'\.def\("(define_[A-Za-z0-9_]+)"', source)
        if route != "define_module"
    )


def python_component_definition_routes() -> list[str]:
    source = read(ROOT / "python" / "volt" / "design.py")
    return sorted(
        route
        for route in set(re.findall(r"self\._circuit\.(define_[A-Za-z0-9_]+)", source))
        if route != "define_module"
    )


def current_format_inventory() -> dict[str, object]:
    return {
        "production_callers": {
            "native_component_definition_routes": bound_component_definition_routes(),
            "python_component_definition_routes": python_component_definition_routes(),
            "tracked_non_example_project_entrypoints": [
                "python/tests/fixtures/project_cli/multiple_boards/project_entry.py",
                "python/tests/fixtures/project_cli/single_board/project_entry.py",
            ],
            "supported_python_authoring_owners": [
                "python/volt/design.py: typed component and exact Part lowering",
                "python/volt/library.py: exact PartLibraryBundle selection",
                "python/volt/project.py: Project orchestration and one native schema 2 publication",
                "python/volt/project_bundle.py: verified current-schema reopen",
                "python/volt/cli/__init__.py: canonical CLI orchestration",
            ],
        },
        "architecture_fixtures": {
            "led": [
                "tests/support/architecture_led_fixture.hpp",
                "tests/circuit/led_circuit_test.cpp",
                "tests/io/logical/logical_circuit_writer_test.cpp",
            ],
            "source_regulator_esp32": [
                "python/tests/test_issue_319_architecture_fixture.py"
            ],
            "board_and_named_alternatives": [
                "python/tests/fixtures/project_cli/multiple_boards/project_entry.py",
                "python/tests/test_cli_project_workflow.py",
            ],
            "part_library_bundle_and_offline_reopen": [
                "python/tests/test_issue_319_architecture_fixture.py"
            ],
            "immutable_compiled_board": [
                "python/tests/test_issue_319_architecture_fixture.py"
            ],
            "project_bundle_schema_2": [
                "python/tests/test_issue_319_architecture_fixture.py",
                "python/tests/test_cli_project_workflow.py",
            ],
            "canonical_q4_cli": [
                "python/tests/fixtures/project_cli/single_board/project_entry.py",
                "python/tests/fixtures/project_cli/multiple_boards/project_entry.py",
                "python/tests/test_cli_project_workflow.py",
            ],
            "vault_board_scene": [
                "python/tests/test_issue_319_architecture_fixture.py"
            ],
            "typed_selected_exports_and_manufacturing": [
                "python/tests/test_cli_project_workflow.py",
                "python/tests/test_issue_319_architecture_fixture.py",
            ],
        },
        "no_hidden_fallback_evidence": {
            "canonical_cli_has_no_post_instantiation_selection": all(
                ".select_part(" not in read(ROOT / path)
                for path in (
                    "python/tests/fixtures/project_cli/multiple_boards/project_entry.py",
                    "python/tests/fixtures/project_cli/single_board/project_entry.py",
                )
            ),
            "cpp_acceptance_has_no_example_include": (
                "${PROJECT_SOURCE_DIR}/examples"
                not in read(ROOT / "tests" / "CMakeLists.txt")
            ),
            "project_bundle_open_never_executes_source": (
                "never_executes_project_or_library_source"
                in read(ROOT / "python" / "tests" / "test_project_bundle_open.py")
            ),
        },
    }


def semantic_shape(payload: object) -> dict[str, object]:
    if not isinstance(payload, dict):
        return {"top_level_keys": [], "top_level_counts": {}, "top_level_scalars": {}}
    top_level_counts = {
        key: len(value) for key, value in payload.items() if isinstance(value, (list, dict))
    }
    top_level_scalars = {
        key: value
        for key, value in payload.items()
        if isinstance(value, (str, int, float, bool)) or value is None
    }
    result: dict[str, object] = {
        "top_level_keys": sorted(payload),
        "top_level_counts": dict(sorted(top_level_counts.items())),
        "top_level_scalars": dict(sorted(top_level_scalars.items())),
    }
    if isinstance(payload.get("artifacts"), list):
        result["artifact_kinds"] = sorted(
            artifact["kind"]
            for artifact in payload["artifacts"]
            if isinstance(artifact, dict) and isinstance(artifact.get("kind"), str)
        )
    return result


def semantic_summary(path: Path) -> dict[str, object]:
    return {
        "path": relative(path),
        "sha256": sha256_bytes(path),
        **semantic_shape(json.loads(read(path))),
    }


def golden_inventory() -> dict[str, object]:
    return {
        "semantic": [semantic_summary(ROOT / path) for path in SEMANTIC_GOLDEN_PATHS],
        "byte": [
            {
                "path": path,
                "byte_count": (ROOT / path).stat().st_size,
                "sha256": sha256_bytes(ROOT / path),
            }
            for path in BYTE_GOLDEN_PATHS
        ],
    }


def collect_inventory() -> dict[str, object]:
    paths = tracked_paths()
    return {
        "schema_version": 4,
        "purpose": (
            "Compact review evidence for issue #328, extended by issue #240 for native verified "
            "ProjectBundle read views, issue #319 for production-consumer ownership, and the "
            "current-format-only zero state. A baseline delta "
            "requires review; the checked-in evidence does not approve the change."
        ),
        "inventories": {
            "public_contracts": {
                "cpp_api_snapshots": {
                    name: snapshot_summary(path)
                    for name, path in sorted(PUBLIC_SNAPSHOTS.items())
                },
                "python_exports": python_all_exports(),
                "python_exceptions": python_exception_inventory(),
            },
            "canonical_artifacts": canonical_artifact_inventory(),
            "ownership": {
                "design_owner_attributes": init_assigned_attributes(
                    ROOT / "python" / "volt" / "design.py", "Design"
                ),
                "python_board_handle_attributes": init_assigned_attributes(
                    ROOT / "python" / "volt" / "pcb.py", "Board"
                ),
                "native_bound_classes": native_bound_classes(),
                "py_circuit_private_members": binding_private_members(
                    "src/python/py_circuit.hpp"
                ),
                "py_board_owner_private_members": binding_private_members(
                    "src/python/py_board.hpp"
                ),
                "py_schematic_private_members": binding_private_members(
                    "src/python/py_schematic.hpp"
                ),
                "lifetime_evidence": [
                    "src/python/board_bindings.cpp: BoardRegistry keep_alive retains its bound Circuit owner",
                    "src/python/board_bindings.cpp: Board references retain their BoardRegistry owner",
                    "src/python/py_board.hpp: BoardRegistry directly owns every separately bound native Board",
                    "src/python/schematic_bindings.cpp: SchematicDocument keep_alive retains its bound Circuit owner",
                    "src/python/py_schematic.hpp: SchematicDocument is a direct bound owner of native SchematicDocument state",
                    "tests/architecture/circuit_public_api.txt: deleted rvalue all() overload freezes borrowed range lifetime",
                    "tests/architecture/board_public_api.txt: deleted rvalue Board(Circuit&&) constructor freezes Circuit borrow lifetime",
                    "tests/architecture/schematic_public_api.txt: deleted rvalue Schematic(Circuit&&) constructor freezes Circuit borrow lifetime",
                ],
                "hidden_footprint_injections": hidden_footprint_injections(),
            },
            "offline_fixtures": offline_fixtures(),
            "current_format": current_format_inventory(),
            "anti_regrowth": anti_regrowth_inventory(paths),
        },
        "goldens": golden_inventory(),
    }


def compare(expected: object, actual: object, path: str = "root") -> list[str]:
    if type(expected) is not type(actual):
        return [f"{path}: type changed from {type(expected).__name__} to {type(actual).__name__}"]
    if isinstance(expected, dict):
        failures: list[str] = []
        expected_keys = set(expected)
        actual_keys = set(actual)
        for key in sorted(expected_keys - actual_keys):
            failures.append(f"{path}.{key}: missing")
        for key in sorted(actual_keys - expected_keys):
            failures.append(f"{path}.{key}: new key")
        for key in sorted(expected_keys & actual_keys):
            failures.extend(compare(expected[key], actual[key], f"{path}.{key}"))
        return failures
    if isinstance(expected, list):
        return [] if expected == actual else [f"{path}: list changed"]
    return [] if expected == actual else [f"{path}: expected {expected!r}, got {actual!r}"]


def run_checks() -> int:
    require_lf_checkout_paths()
    actual = collect_inventory()
    if not BASELINE_PATH.exists():
        print(
            f"{relative(BASELINE_PATH)} is missing; run with --write-baseline after reviewing output",
            file=sys.stderr,
        )
        return 1
    failures = compare(json.loads(read(BASELINE_PATH)), actual)
    if failures:
        for failure in failures:
            print(
                "architecture review evidence drift: "
                f"{failure}; review the delta before updating {relative(BASELINE_PATH)}. "
                "The baseline does not approve the change.",
                file=sys.stderr,
            )
        return 1
    return 0


def write_baseline() -> int:
    BASELINE_PATH.parent.mkdir(parents=True, exist_ok=True)
    BASELINE_PATH.write_text(
        json.dumps(collect_inventory(), indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(relative(BASELINE_PATH))
    return 0


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_writer_ownership_rejection(
    project_source: str,
    message: str,
) -> None:
    try:
        current_project_writer_ownership(project_source)
    except AssertionError:
        return
    raise AssertionError(message)


def run_self_tests() -> int:
    inventory = collect_inventory()
    require(
        set(inventory) == {"schema_version", "purpose", "inventories", "goldens"},
        "the inventory must expose only the simplified evidence schema",
    )
    require(
        set(inventory["inventories"])
        == {
            "anti_regrowth",
            "public_contracts",
            "canonical_artifacts",
            "ownership",
            "offline_fixtures",
            "current_format",
        },
        "the inventory must contain durable contract, artifact, ownership, fixture, and "
        "current-format evidence",
    )
    serialized_inventory = json.dumps(inventory, sort_keys=True)
    require(
        "caller_strata" not in serialized_inventory
        and "future_slice" not in serialized_inventory
        and "approved_references" not in serialized_inventory,
        "caller counts and future-slice approval metadata must not survive",
    )
    production = inventory["inventories"]["current_format"]
    expected_routes = {
        "define_capacitor",
        "define_component",
        "define_connector_1x01",
        "define_connector_1x02",
        "define_connector_1x03",
        "define_crystal_2pin",
        "define_diode",
        "define_inductor",
        "define_led",
        "define_library_part",
        "define_op_amp_5pin",
        "define_polarized_capacitor",
        "define_regulator_3pin",
        "define_resistor",
        "define_switch_spst",
        "define_test_point",
    }
    require(
        set(production["production_callers"]["native_component_definition_routes"])
        == expected_routes
        and set(production["production_callers"]["python_component_definition_routes"])
        == expected_routes,
        "every supported Python component and exact Part route must reach the native boundary",
    )
    require(
        all(production["no_hidden_fallback_evidence"].values()),
        "current-format acceptance fixtures must not depend on retired selection or source execution",
    )
    require(
        inventory["inventories"]["anti_regrowth"]
        == {"forbidden_paths": [], "retired_route_references": []},
        "M2 must leave no retired path or route in the current tree",
    )
    try:
        require_tracked_paths_exist(["tests/fixtures/__missing_tracked_path__"])
    except AssertionError as error:
        require(
            "__missing_tracked_path__" in str(error),
            "missing tracked paths must be named in the governance failure",
        )
    else:
        raise AssertionError("the inventory must reject tracked paths missing from the worktree")
    require(
        anti_regrowth_text_violations(
            {
                "python/volt/project.py": "class ProjectArtifactPaths:\n    pass\n",
                "docs/current.md": "call result.write_artifacts(output)\n",
                "python/volt/library.py": "class LibraryComponent:\n    pass\n",
                "python/volt/cli/__init__.py": 'parser.add_argument("--flat")\n',
                "python/volt/cli/retired.py": "COMMAND = 'volt run --project .'\n",
                "python/volt/cli/manufacturing.py": "COMMAND = 'volt export manufacturing'\n",
                "skills/current.md": "component.select_part()\n",
            }
        )
        == [
            "retired_flat_writer:docs/current.md:1",
            "retired_flat_cli:python/volt/cli/__init__.py:1",
            "retired_source_manufacturing_cli:python/volt/cli/manufacturing.py:1",
            "retired_source_cli_alias:python/volt/cli/retired.py:1",
            "retired_library_component:python/volt/library.py:1",
            "retired_flat_writer_paths:python/volt/project.py:1",
            "retired_instance_selection:skills/current.md:1",
        ],
        "the anti-regrowth checker must detect each retired current route",
    )
    require(
        anti_regrowth_text_violations(
            {
                "python/volt/current.py": (
                    "import examples.retired\n"
                    "from examples.other import fixture\n"
                    "fixture = 'examples/retired/project.py'\n"
                ),
            }
        )
        == [
            "retired_example_reference:python/volt/current.py:1",
            "retired_example_reference:python/volt/current.py:2",
            "retired_example_reference:python/volt/current.py:3",
        ],
        "the anti-regrowth checker must detect dotted and slash retired example references",
    )
    require(
        not anti_regrowth_text_violations(
            {
                "include/native.hpp": "LibraryComponentRef owner;\nOrderablePart part;\n",
                "docs/current.md": (
                    "Use Library.part() and ProjectResult.write().\n"
                    "See the following examples.\n"
                    'These "examples" describe the current API.\n'
                ),
            }
        ),
        "the anti-regrowth checker must accept current vocabulary and ordinary example prose",
    )
    require(
        parse_check_attr_eol(b"one\0eol\0lf\0two\0eol\0unspecified\0")
        == {"one": "lf", "two": "unspecified"},
        "check-attr output must preserve each checked path's eol policy",
    )
    require(
        set(snapshot_summary(PUBLIC_SNAPSHOTS["Circuit"])) == {"path", "sha256"},
        "snapshot evidence must stay compact and content-addressed",
    )

    fixture = textwrap.dedent(
        """
        py::dict PyBoard::to_json() const {
            return write(board_, volt::builtin_footprint_library());
        }
        """
    )
    match = re.search(r"volt::builtin_footprint_library\s*\(\)", fixture)
    require(match is not None, "hidden footprint injection sample must be detected")
    require(
        current_cpp_function(fixture, match.start()) == "PyBoard::to_json",
        "hidden footprint injection callers must be attributed to the owning PyBoard method",
    )

    semantic = {"format": "volt.test", "version": 1, "items": [1, 2], "metadata": {"a": 1}}
    require(
        semantic_shape(semantic)
        == {
            "top_level_keys": ["format", "items", "metadata", "version"],
            "top_level_counts": {"items": 2, "metadata": 1},
            "top_level_scalars": {"format": "volt.test", "version": 1},
        },
        "semantic summaries must freeze top-level scalars and collection sizes",
    )

    project_source = read(ROOT / "python" / "volt" / "project.py")
    ownership = current_project_writer_ownership(project_source)
    require(
        ownership["native_board_preparation"] == "_volt._prepare_project_bundle_board"
        and ownership["retired_python_artifact_record"] is False,
        "current writer evidence must retain native schema 2 ownership and reject a second writer",
    )

    bypassed_project = project_source.replace(
        "_volt._write_project_bundle_v2(",
        "self.write_artifacts(",
        1,
    )
    require_writer_ownership_rejection(
        bypassed_project,
        "the ownership gate must reject bypassing the native v2 route",
    )

    restored_flat_writer = project_source.replace(
        "        return _volt._write_project_bundle_v2(",
        "        self.write_artifacts(path)\n"
        "        return _volt._write_project_bundle_v2(",
        1,
    )
    require_writer_ownership_rejection(
        restored_flat_writer,
        "the ownership gate must reject restoring a current Python flat writer",
    )

    restored_artifact_record = project_source.replace(
        "        return _volt._write_project_bundle_v2(",
        '        _artifact_record("logical")\n'
        "        return _volt._write_project_bundle_v2(",
        1,
    )
    require_writer_ownership_rejection(
        restored_artifact_record,
        "the ownership gate must reject restoring the retired Python artifact-record writer",
    )

    bypassed_board_preparation = project_source.replace(
        "_volt._prepare_project_bundle_board(",
        "(",
        1,
    )
    require_writer_ownership_rejection(
        bypassed_board_preparation,
        "the ownership gate must reject bypassing complete native Board preparation",
    )

    bypassed_delegation = project_source.replace(
        "        self._write_selected_bundle(",
        "        self.write_artifacts(",
        1,
    )
    require_writer_ownership_rejection(
        bypassed_delegation,
        "the ownership gate must reject bypassing the one shared native publication helper",
    )
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="run checker self-tests")
    parser.add_argument(
        "--write-baseline",
        action="store_true",
        help="rewrite the checked-in evidence after reviewing the current source-tree delta",
    )
    args = parser.parse_args(argv)
    if args.self_test:
        return run_self_tests()
    if args.write_baseline:
        return write_baseline()
    return run_checks()


if __name__ == "__main__":
    raise SystemExit(main())
