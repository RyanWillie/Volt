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

    return {
        "entrypoint": "ProjectResult.write",
        "legacy_python_artifact_record": False,
        "native_board_preparation": "_volt._prepare_project_bundle_board",
        "native_binding": "_volt._write_project_bundle_v2",
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
            "Compact review evidence for issue #328, extended by issue #240 for native verified "
            "ProjectBundle read views. A baseline delta requires review; the checked-in evidence "
            "does not approve the change."
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
    ownership = current_project_writer_ownership(project_source)
    require(
        ownership["native_board_preparation"] == "_volt._prepare_project_bundle_board"
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
        "the ownership gate must reject restoring the Python v1 artifact-record writer",
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
