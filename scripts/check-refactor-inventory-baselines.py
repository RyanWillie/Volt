#!/usr/bin/env python3
"""Check compact architecture review evidence and durable compatibility inventories."""

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
    "examples/stm32_usb_buck/artifacts/stm32_usb_buck.volt/manifest.volt.json",
    "examples/stm32_usb_buck/artifacts/stm32_usb_buck.volt/logical/stm32_usb_buck.volt.json",
    "examples/stm32_usb_buck/artifacts/stm32_usb_buck.volt/schematic/STM32-USB-Buck.volt.schematic.json",
    "examples/stm32_usb_buck/artifacts/stm32_usb_buck.volt/pcb/STM32-USB-Buck-PCB.volt.pcb.json",
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
    "examples/stm32_usb_buck/artifacts/stm32_usb_buck.cpl.csv",
    "examples/stm32_usb_buck/artifacts/stm32_usb_buck.cpl.json",
    "examples/stm32_usb_buck/artifacts/stm32_usb_buck.kicad_pcb",
    "examples/stm32_usb_buck/artifacts/stm32_usb_buck.pcb.svg",
    "examples/stm32_usb_buck/artifacts/stm32_usb_buck.volt/pcb/STM32-USB-Buck-PCB.volt.pcb.json",
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


def dataclass_fields(path: Path, class_name: str) -> list[str]:
    tree = ast.parse(read(path), filename=relative(path))
    for node in tree.body:
        if not isinstance(node, ast.ClassDef) or node.name != class_name:
            continue
        return sorted(
            child.target.id
            for child in node.body
            if isinstance(child, ast.AnnAssign) and isinstance(child.target, ast.Name)
        )
    return []


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


def cpp_parenthesized_call(source: str, marker: str) -> str:
    marker_offset = source.find(marker)
    require(marker_offset >= 0, f"C++ binding marker is missing: {marker}")
    call_offset = source.rfind("module.def", 0, marker_offset)
    require(call_offset >= 0, f"C++ binding call is missing for: {marker}")
    open_offset = source.find("(", call_offset, marker_offset)
    require(open_offset >= 0, f"C++ binding call has no argument list: {marker}")

    depth = 0
    quote: str | None = None
    escaped = False
    line_comment = False
    block_comment = False
    index = open_offset
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if line_comment:
            if char == "\n":
                line_comment = False
        elif block_comment:
            if char == "*" and next_char == "/":
                block_comment = False
                index += 1
        elif quote is not None:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
        elif char == "/" and next_char == "/":
            line_comment = True
            index += 1
        elif char == "/" and next_char == "*":
            block_comment = True
            index += 1
        elif char in {'"', "'"}:
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return source[call_offset : index + 1]
        index += 1
    raise AssertionError(f"C++ binding call is unterminated: {marker}")


def cpp_code_only(source: str) -> str:
    result: list[str] = []
    quote: str | None = None
    escaped = False
    line_comment = False
    block_comment = False
    index = 0
    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""
        if line_comment:
            result.append("\n" if char == "\n" else " ")
            if char == "\n":
                line_comment = False
        elif block_comment:
            result.append("\n" if char == "\n" else " ")
            if char == "*" and next_char == "/":
                result.append(" ")
                block_comment = False
                index += 1
        elif quote is not None:
            result.append("\n" if char == "\n" else " ")
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
        elif char == "/" and next_char == "/":
            result.extend((" ", " "))
            line_comment = True
            index += 1
        elif char == "/" and next_char == "*":
            result.extend((" ", " "))
            block_comment = True
            index += 1
        elif char in {'"', "'"}:
            result.append(" ")
            quote = char
        else:
            result.append(char)
        index += 1
    return "".join(result)


def current_project_writer_ownership(
    project_source: str,
    binding_source: str,
    registration_source: str,
) -> dict[str, object]:
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

    calls = [node for node in ast.walk(write_method) if isinstance(node, ast.Call)]
    call_names = [qualified_python_name(call.func) for call in calls]
    native_calls = [
        call
        for call, name in zip(calls, call_names)
        if name == "_volt._write_project_bundle_v2"
    ]
    require(
        len(native_calls) == 1,
        "ProjectResult.write must call the native typed v2 writer exactly once",
    )
    native_call = native_calls[0]
    require(
        any(
            isinstance(statement, ast.Expr) and statement.value is native_call
            for statement in write_method.body
        ),
        "ProjectResult.write must invoke the native typed v2 writer directly",
    )
    board_preparation_calls = [
        call
        for call, name in zip(calls, call_names)
        if name == "_volt._prepare_project_bundle_board"
    ]
    require(
        len(board_preparation_calls) == 1,
        "ProjectResult.write must prepare complete native Board artifacts exactly once",
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
        "ProjectResult.write may only prepare typed native inputs before publication; "
        f"unexpected calls: {unexpected_calls}",
    )

    native_nodes = set(ast.walk(native_call))
    path_uses = [
        node for node in ast.walk(write_method) if isinstance(node, ast.Name) and node.id == "path"
    ]
    require(path_uses, "ProjectResult.write must consume its requested destination")
    require(
        all(node in native_nodes for node in path_uses),
        "ProjectResult.write may pass its destination only to the native typed v2 writer",
    )

    legacy_definitions = [
        node
        for node in ast.walk(tree)
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef))
        and node.name == "_artifact_record"
    ]
    legacy_calls = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.Call)
        and qualified_python_name(node.func) in {"_artifact_record", "self._artifact_record"}
    ]
    require(
        not legacy_definitions and not legacy_calls,
        "the legacy Python _artifact_record/v1 writer must not exist in the current Project path",
    )

    binding_call = cpp_code_only(
        cpp_parenthesized_call(binding_source, '"_write_project_bundle_v2"')
    )
    require(
        re.search(r"\bvolt::io::ProjectBundleV2Builder\s*\{", binding_call) is not None,
        "the native v2 binding must construct ProjectBundleV2Builder",
    )
    require(
        re.search(r"\bauto\s+bundle\s*=\s*builder\.build\s*\(\s*\)\s*;", binding_call)
        is not None,
        "the native v2 binding must build one validated ProjectBundleV2",
    )
    require(
        re.search(r"\bbundle\.write\s*\(\s*destination\s*\)\s*;", binding_call) is not None,
        "the native v2 binding must publish through ProjectBundleV2::write",
    )
    require(
        "compile_for_delivery" not in binding_call and "prepare_board_scene" not in binding_call,
        "the native v2 writer must consume complete Board artifacts rather than create them",
    )
    require(
        re.search(r"\bvolt::io::ProjectBundle(?!V2)\b", binding_call) is None
        and "ProjectBundleSchemaVersion::V1" not in binding_call,
        "the current native binding must not route through ProjectBundle v1",
    )
    require(
        re.search(
            r"\bvolt::python::bind_project_bundle\s*\(\s*module\s*\)\s*;",
            cpp_code_only(registration_source),
        )
        is not None,
        "the native v2 ProjectBundle binding must remain registered",
    )
    return {
        "entrypoint": "ProjectResult.write",
        "legacy_python_artifact_record": False,
        "native_board_preparation": "_volt._prepare_project_bundle_board",
        "native_binding": "_volt._write_project_bundle_v2",
        "native_builder": "volt::io::ProjectBundleV2Builder",
        "native_publication": "ProjectBundleV2::write",
    }


def manifest_payloads() -> dict[str, dict[str, object]]:
    return {
        relative(path): json.loads(read(path))
        for path in sorted((ROOT / "examples").glob("*/artifacts/**/*.volt.json"))
        if path.name == "manifest.volt.json"
    }


def manifest_artifact_kinds(payloads: dict[str, dict[str, object]]) -> list[str]:
    return sorted(
        {
            artifact["kind"]
            for payload in payloads.values()
            for artifact in payload.get("artifacts", [])
            if isinstance(artifact, dict) and isinstance(artifact.get("kind"), str)
        }
    )


def canonical_artifact_inventory() -> dict[str, object]:
    payloads = manifest_payloads()
    return {
        "current_writer_ownership": current_project_writer_ownership(
            read(ROOT / "python" / "volt" / "project.py"),
            read(ROOT / "src" / "python" / "project_bundle_bindings.cpp"),
            read(ROOT / "src" / "python" / "bindings.cpp"),
        ),
        "project_artifact_path_fields": dataclass_fields(
            ROOT / "python" / "volt" / "project.py", "ProjectArtifactPaths"
        ),
        "manifest_artifact_kinds": manifest_artifact_kinds(payloads),
        "manifest_fields": {
            path: sorted(payload) for path, payload in sorted(payloads.items())
        },
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
    return {
        "schema_version": 2,
        "purpose": (
            "Compact review evidence for issue #328. A baseline delta requires review; "
            "the checked-in evidence does not approve the change."
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
    binding_source: str,
    registration_source: str,
    message: str,
) -> None:
    try:
        current_project_writer_ownership(
            project_source,
            binding_source,
            registration_source,
        )
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
        == {"public_contracts", "canonical_artifacts", "ownership", "offline_fixtures"},
        "the inventory must contain only durable contract, artifact, ownership, and fixture evidence",
    )
    serialized_inventory = json.dumps(inventory, sort_keys=True)
    require(
        "caller_strata" not in serialized_inventory
        and "future_slice" not in serialized_inventory
        and "approved_references" not in serialized_inventory,
        "caller counts and future-slice approval metadata must not survive",
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
    binding_source = read(ROOT / "src" / "python" / "project_bundle_bindings.cpp")
    registration_source = read(ROOT / "src" / "python" / "bindings.cpp")
    ownership = current_project_writer_ownership(
        project_source,
        binding_source,
        registration_source,
    )
    require(
        ownership["native_builder"] == "volt::io::ProjectBundleV2Builder"
        and ownership["native_board_preparation"] == "_volt._prepare_project_bundle_board"
        and ownership["legacy_python_artifact_record"] is False,
        "current writer evidence must retain native v2 ownership and reject Python v1",
    )

    bypassed_project = project_source.replace(
        "_volt._write_project_bundle_v2(",
        "self.write_artifacts(",
        1,
    )
    require_writer_ownership_rejection(
        bypassed_project,
        binding_source,
        registration_source,
        "the ownership gate must reject bypassing the native v2 route",
    )

    restored_flat_writer = project_source.replace(
        "        _volt._write_project_bundle_v2(",
        "        self.write_artifacts(path)\n        _volt._write_project_bundle_v2(",
        1,
    )
    require_writer_ownership_rejection(
        restored_flat_writer,
        binding_source,
        registration_source,
        "the ownership gate must reject restoring a current Python flat writer",
    )

    restored_artifact_record = project_source.replace(
        "        _volt._write_project_bundle_v2(",
        '        _artifact_record("logical")\n        _volt._write_project_bundle_v2(',
        1,
    )
    require_writer_ownership_rejection(
        restored_artifact_record,
        binding_source,
        registration_source,
        "the ownership gate must reject restoring the Python v1 artifact-record writer",
    )

    bypassed_board_preparation = project_source.replace(
        "_volt._prepare_project_bundle_board(",
        "(",
        1,
    )
    require_writer_ownership_rejection(
        bypassed_board_preparation,
        binding_source,
        registration_source,
        "the ownership gate must reject bypassing complete native Board preparation",
    )

    writer_owned_compilation = binding_source.replace(
        "                const auto &board = artifacts->board();\n",
        "                const auto &board = artifacts->board();\n"
        "                static_cast<void>(board.compile_for_delivery(false));\n",
        1,
    )
    require_writer_ownership_rejection(
        project_source,
        writer_owned_compilation,
        registration_source,
        "the ownership gate must reject moving Board compilation into the v2 writer",
    )

    missing_native_publication = binding_source.replace(
        "            bundle.write(destination);\n",
        "",
        1,
    )
    require_writer_ownership_rejection(
        project_source,
        missing_native_publication,
        registration_source,
        "the ownership gate must reject a native binding that does not publish ProjectBundleV2",
    )
    spoofed_native_publication = missing_native_publication.replace(
        "            auto result = py::dict{};\n",
        "            // bundle.write(destination);\n            auto result = py::dict{};\n",
        1,
    )
    require_writer_ownership_rejection(
        project_source,
        spoofed_native_publication,
        registration_source,
        "the ownership gate must ignore native-publication text in comments",
    )

    missing_registration = registration_source.replace(
        "    volt::python::bind_project_bundle(module);\n",
        "",
        1,
    )
    require_writer_ownership_rejection(
        project_source,
        binding_source,
        missing_registration,
        "the ownership gate must reject removing the native ProjectBundle binding",
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
