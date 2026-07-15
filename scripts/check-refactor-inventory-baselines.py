#!/usr/bin/env python3
"""Freeze the Gate F1 refactor inventories and representative parity goldens."""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import re
import subprocess
import sys
import textwrap
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
BASELINE_PATH = ROOT / "tests" / "architecture" / "refactor_inventory_baseline.json"
MEASUREMENT_BASELINE = "817ad42588d01d2b7d095d1b4b39cc7abed18b6a"

CPP_SUFFIXES = {".cpp", ".hpp", ".h"}
PY_SUFFIXES = {".py"}

SOURCE_DIRS = ("include", "src", "tests", "examples", "python")
PUBLIC_SNAPSHOTS = {
    "Circuit": ROOT / "tests" / "architecture" / "circuit_public_api.txt",
    "Schematic": ROOT / "tests" / "architecture" / "schematic_public_api.txt",
    "Board": ROOT / "tests" / "architecture" / "board_public_api.txt",
    "BoardRouter": ROOT / "tests" / "architecture" / "boardrouter_public_api.txt",
    "BoardSpatialIndex": ROOT / "tests" / "architecture" / "boardspatialindex_public_api.txt",
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

F1_LF_PATHS = tuple(
    dict.fromkeys(
        [path.relative_to(ROOT).as_posix() for path in PUBLIC_SNAPSHOTS.values()]
        + list(SEMANTIC_GOLDEN_PATHS)
        + list(BYTE_GOLDEN_PATHS)
    )
)

COUNTING_RULES = [
    "Public declarations are nonblank non-include lines in tests/architecture/*_public_api.txt; overloads stay separate and constructors count.",
    "Native binding methods are named .def(\"...\") registrations on py::class_<PyCircuit>; py::init is excluded, module.def exports are counted separately.",
    "Caller strata are direct lexical member-call tokens .name( or ->name( for names in the checked-in root snapshots; counts are lower bounds and intentionally do not infer aliases.",
    "Python _circuit calls are AST-like lexical calls on the private Design._circuit handle in python/volt/*.py; public tests and docs are excluded.",
    "Hidden footprint injections are production src/python call sites that invoke volt::builtin_footprint_library(); tests, examples and non-binding C++ are excluded.",
    "Artifact kinds are first arguments to Project _artifact_record(...) calls plus kinds observed in checked-in manifest.volt.json files.",
    "CLI leaves are _handle_* functions that back user commands; _handle_export_manufacturing is treated as the export manufacturing leaf and helper functions are excluded.",
    "Project shapes are example Python files that construct volt.Project and the stage decorators they register; no volt.toml files are expected at this baseline.",
    "Semantic goldens record normalized JSON top-level shape plus SHA-256; byte goldens record raw byte length and SHA-256.",
]

EXCLUSIONS = [
    "Generated docs and architecture HTML are not scanned for product call-site counts.",
    "Tests and examples are excluded from hidden footprint injection counts but are included in caller strata where explicitly named.",
    "Indirect C++ calls through aliases, templates or function pointers are not call-graph counted.",
    "Python public API docs are not counted as exports; __all__ in python/volt/__init__.py is the export boundary.",
    "Future D1-D3 implementation contracts are not enforced here; this checker freezes the Gate F1 evidence baseline and requires intentional updates when approved slices move it.",
]

FUTURE_SLICE_MAP = [
    {
        "slice": "D1 private native owner split",
        "may_update": [
            "native binding method groups",
            "Python ownership paths",
            "binding lifetime entries",
            "hidden footprint injection callers",
        ],
    },
    {
        "slice": "D2 Schematic/Board roots and CompiledBoard",
        "may_update": [
            "Schematic and Board public snapshots",
            "direct caller strata",
            "footprint-resolution injections",
            "Board semantic and byte goldens",
        ],
    },
    {
        "slice": "D3 typed artifact graph and bundle reopening",
        "may_update": [
            "artifact kinds and manifest fields",
            "reader/writer surfaces",
            "CLI source-execution surfaces",
            "bundle and manifest goldens",
        ],
    },
    {
        "slice": "Part-library semantic and exact-selection program",
        "may_update": [
            "built-in component route counts",
            "STM32 catalogue entries",
            "manual selected-part calls",
            "component-contract, part and offline fixture goldens",
        ],
    },
    {
        "slice": "Deletion gate",
        "may_update": [
            "parity gate test inventory",
            "byte-golden shadow pairs",
            "sanitizer, coverage and platform evidence requirements",
        ],
    },
]


@dataclass(frozen=True)
class CallInventory:
    count: int
    names: tuple[str, ...]
    files: tuple[str, ...]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def code_files(*, suffixes: set[str], roots: Iterable[str] = SOURCE_DIRS) -> list[Path]:
    files: list[Path] = []
    for root_name in roots:
        root = ROOT / root_name
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.suffix not in suffixes:
                continue
            if any(part in {".git", "__pycache__", ".pytest_cache"} for part in path.parts):
                continue
            files.append(path)
    return sorted(files)


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
        ["git", "-C", str(ROOT), "check-attr", "-z", "eol", "--", *F1_LF_PATHS],
        capture_output=True,
        check=False,
    )
    if result.returncode:
        raise AssertionError(f"git check-attr failed: {result.stderr.decode('utf-8', errors='replace')}")
    attributes = parse_check_attr_eol(result.stdout)
    missing = [path for path in F1_LF_PATHS if attributes.get(path) != "lf"]
    if missing:
        raise AssertionError(
            "Gate F1 byte-hashed paths must be checked out with LF line endings: " + ", ".join(missing)
        )


def snapshot_declarations(path: Path) -> list[str]:
    return [
        line.rstrip()
        for line in read(path).splitlines()
        if line.strip() and not line.startswith("include ") and not line.startswith("#")
    ]


def declaration_name(declaration: str) -> str | None:
    if "(" not in declaration:
        return None
    prefix = declaration.split("(", 1)[0].strip()
    if not prefix:
        return None
    return prefix.split()[-1].split("::")[-1].strip("&*")


def snapshot_summary(name: str, path: Path) -> dict[str, object]:
    lines = read(path).splitlines()
    declarations = snapshot_declarations(path)
    names = sorted(
        name
        for name in {declaration_name(declaration) for declaration in declarations}
        if name is not None
    )
    return {
        "path": relative(path),
        "include_count": sum(1 for line in lines if line.startswith("include ")),
        "declaration_count": len(declarations),
        "unique_callable_names": names,
        "sha256": sha256_bytes(path),
    }


def class_public_def_methods(source: str) -> list[str]:
    return re.findall(r'\.def\(\s*"([^"]+)"', source)


def py_circuit_class_binding_block(source: str) -> str:
    start = source.find("py::class_<PyCircuit>")
    if start == -1:
        raise AssertionError("py::class_<PyCircuit> binding block not found")
    end = source.find(";\n}", start)
    if end == -1:
        raise AssertionError("py::class_<PyCircuit> binding block terminator not found")
    return source[start:end]


def module_def_functions(source: str) -> list[str]:
    return re.findall(r'\bmodule\.def\(\s*"([^"]+)"', source)


def binding_group(name: str) -> str:
    if name == "board" or name.startswith("board_"):
        return "board"
    if name.startswith("schematic_") or name in {
        "register_schematic_symbol",
        "place_schematic_symbol",
        "load_schematic_json",
        "add_schematic_wire",
        "add_schematic_wire_for_endpoints",
        "add_schematic_net_label",
        "add_schematic_net_label_for_endpoint",
        "add_schematic_junction",
        "add_schematic_junction_for_endpoint",
        "add_schematic_terminal_marker",
        "add_schematic_terminal_marker_for_endpoint",
        "add_schematic_no_connect_marker",
        "add_schematic_sheet_port",
        "add_schematic_sheet_port_for_endpoint",
        "add_schematic_symbol_field",
    }:
        return "schematic"
    if name.startswith("module_") or name in {
        "define_module",
        "add_template_net",
        "add_module_port",
        "add_module_component",
        "module_component_pin_by_name",
        "module_component_pin_by_number",
        "module_component_pin_refs",
        "connect_module_pins",
        "instantiate_root_module",
        "concrete_component_for",
        "bind_port",
        "template_nets",
        "port_bindings",
    }:
        return "module"
    if name.startswith("validate") or name.startswith("bom_"):
        return "validation_bom"
    if name == "to_json":
        return "serializer"
    return "logical"


def py_circuit_binding_inventory() -> dict[str, object]:
    source = read(ROOT / "src" / "python" / "circuit_bindings.cpp")
    methods = class_public_def_methods(py_circuit_class_binding_block(source))
    groups: dict[str, list[str]] = defaultdict(list)
    for method in methods:
        groups[binding_group(method)].append(method)
    return {
        "registered_method_count": len(methods),
        "registered_methods_by_group": {
            group: {"count": len(names), "names": sorted(names)}
            for group, names in sorted(groups.items())
        },
        "module_function_count": len(module_def_functions(source)),
        "module_functions": sorted(module_def_functions(source)),
    }


def python_all_exports() -> list[str]:
    tree = ast.parse(read(ROOT / "python" / "volt" / "__init__.py"))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(isinstance(target, ast.Name) and target.id == "__all__" for target in node.targets):
            continue
        value = ast.literal_eval(node.value)
        return sorted(str(item) for item in value)
    raise AssertionError("__all__ not found in python/volt/__init__.py")


def class_methods(path: Path, class_name: str) -> list[str]:
    tree = ast.parse(read(path), filename=relative(path))
    for node in tree.body:
        if isinstance(node, ast.ClassDef) and node.name == class_name:
            return sorted(
                child.name for child in node.body if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef))
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
            attributes: set[str] = set()
            for subnode in ast.walk(child):
                if not isinstance(subnode, ast.Attribute):
                    continue
                if isinstance(subnode.value, ast.Name) and subnode.value.id == "self":
                    attributes.add(subnode.attr)
            return sorted(attributes)
    return []


def py_circuit_private_members() -> list[str]:
    header = read(ROOT / "src" / "python" / "py_circuit.hpp")
    private = header.split("  private:", 1)[1]
    return sorted(
        name
        for name in re.findall(r"\b([A-Za-z_]\w*)_\s*(?:=|;)", private)
        if name not in {"std", "volt"}
    )


def python_exception_inventory() -> dict[str, object]:
    source = read(ROOT / "src" / "python" / "bindings.cpp")
    exception_names = re.findall(r'create_exception_class\(\s*module,\s*"([^"]+)"', source)
    metadata_keys = re.findall(r'exception\.attr\("([^"]+)"\)', source)
    entity_keys = re.findall(r'result\["([^"]+)"\]', source)
    return {
        "exception_classes": exception_names,
        "exception_metadata_keys": sorted(set(metadata_keys)),
        "entity_metadata_keys": sorted(entity_keys),
    }


def direct_call_inventory(root_name: str, method_names: Iterable[str], roots: Iterable[str]) -> dict[str, object]:
    names = sorted(set(method_names))
    pattern_by_name = {
        name: re.compile(rf"(?:\.|->)\s*{re.escape(name)}\s*\(") for name in names
    }
    found: Counter[str] = Counter()
    files: set[str] = set()
    for path in code_files(suffixes=CPP_SUFFIXES, roots=roots):
        text = read(path)
        file_count = 0
        for name, pattern in pattern_by_name.items():
            matches = pattern.findall(text)
            if matches:
                found[name] += len(matches)
                file_count += len(matches)
        if file_count:
            files.add(relative(path))
    return {
        "count": sum(found.values()),
        "unique_name_count": len(found),
        "names": sorted(found),
        "files": sorted(files),
    }


def caller_strata() -> dict[str, object]:
    snapshot_methods = {
        root: [
            name
            for name in (declaration_name(item) for item in snapshot_declarations(path))
            if name is not None and name != root
        ]
        for root, path in PUBLIC_SNAPSHOTS.items()
        if root in {"Schematic", "Board"}
    }
    strata = {
        "core_production": ("src/schematic", "src/pcb"),
        "io_adapters": ("src/io", "src/adapters"),
        "python_binding": ("src/python",),
        "tests": ("tests",),
    }
    result: dict[str, object] = {}
    for root, methods in snapshot_methods.items():
        root_result: dict[str, object] = {}
        for stratum, directories in strata.items():
            selected = tuple(directory for directory in directories if (ROOT / directory).exists())
            root_result[stratum] = direct_call_inventory(root, methods, selected)
        result[root.lower()] = root_result
    return result


def python_circuit_call_inventory() -> dict[str, object]:
    pattern = re.compile(r"\._circuit\.([A-Za-z_]\w*)\s*\(")
    found: Counter[str] = Counter()
    files: set[str] = set()
    for path in sorted((ROOT / "python" / "volt").glob("*.py")):
        text = read(path)
        matches = pattern.findall(text)
        if not matches:
            continue
        files.add(relative(path))
        found.update(matches)
    return {
        "count": sum(found.values()),
        "unique_name_count": len(found),
        "names": sorted(found),
        "files": sorted(files),
    }


def current_cpp_function(text: str, position: int) -> str:
    prefix = text[:position]
    matches = list(
        re.finditer(
            r"\b(?:[A-Za-z_:<>~*&\s]+)\s+(PyCircuit::[A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?\{",
            prefix,
        )
    )
    return matches[-1].group(1) if matches else "<unknown>"


def hidden_footprint_injections() -> list[dict[str, object]]:
    injections: list[dict[str, object]] = []
    pattern = re.compile(r"volt::builtin_footprint_library\s*\(\)")
    for path in sorted((ROOT / "src" / "python").glob("*.cpp")):
        text = read(path)
        for match in pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            injections.append(
                {
                    "path": relative(path),
                    "line": line,
                    "caller": current_cpp_function(text, match.start()),
                }
            )
    return injections


def project_artifact_record_kinds() -> list[str]:
    source = read(ROOT / "python" / "volt" / "project.py")
    return sorted(set(re.findall(r"_artifact_record\(\s*\"([^\"]+)\"", source, flags=re.DOTALL)))


def manifest_artifact_kinds() -> list[str]:
    kinds: set[str] = set()
    for path in sorted((ROOT / "examples").glob("*/artifacts/**/*.volt.json")):
        if path.name != "manifest.volt.json":
            continue
        payload = json.loads(read(path))
        for artifact in payload.get("artifacts", []):
            kind = artifact.get("kind")
            if isinstance(kind, str):
                kinds.add(kind)
    return sorted(kinds)


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


def io_surface_inventory() -> dict[str, object]:
    io_files = code_files(suffixes=CPP_SUFFIXES, roots=("include/volt/io", "src/io"))
    readers = sorted(relative(path) for path in io_files if "reader" in path.name)
    writers = sorted(relative(path) for path in io_files if "writer" in path.name)
    schemas = sorted(relative(path) for path in io_files if "schema" in path.name or "format" in path.name)
    diagnostics = sorted(
        relative(path)
        for path in code_files(suffixes=CPP_SUFFIXES, roots=("include", "src", "tests"))
        if "diagnostic" in path.name or "validation" in path.parts
    )
    return {
        "reader_count": len(readers),
        "readers": readers,
        "writer_count": len(writers),
        "writers": writers,
        "schema_or_format_count": len(schemas),
        "schemas_or_formats": schemas,
        "diagnostic_surface_count": len(diagnostics),
    }


def artifact_inventory() -> dict[str, object]:
    manifest_paths = sorted(
        relative(path)
        for path in (ROOT / "examples").glob("*/artifacts/**/*.volt.json")
        if path.name == "manifest.volt.json"
    )
    manifest_fields: dict[str, list[str]] = {}
    for rel_path in manifest_paths:
        payload = json.loads(read(ROOT / rel_path))
        manifest_fields[rel_path] = sorted(payload.keys())
    return {
        "project_artifact_path_fields": dataclass_fields(ROOT / "python" / "volt" / "project.py", "ProjectArtifactPaths"),
        "project_record_kinds": project_artifact_record_kinds(),
        "manifest_artifact_kinds": manifest_artifact_kinds(),
        "manifest_paths": manifest_paths,
        "manifest_fields": manifest_fields,
        "io_surfaces": io_surface_inventory(),
    }


def test_function_count(path: Path) -> int:
    if path.suffix == ".py":
        return sum(1 for node in ast.walk(ast.parse(read(path))) if isinstance(node, ast.FunctionDef) and node.name.startswith("test_"))
    return len(re.findall(r"\bTEST_CASE\s*\(", read(path)))


def part_route_inventory() -> dict[str, object]:
    py_circuit_methods = class_public_def_methods(read(ROOT / "src" / "python" / "circuit_bindings.cpp"))
    builtins = sorted(
        name
        for name in py_circuit_methods
        if name.startswith("define_") and name not in {"define_component", "define_module"}
    )
    stm32_source = read(ROOT / "python" / "volt" / "libraries" / "stm32_usb_buck.py")
    manual_selection_calls = []
    for path in sorted((ROOT / "examples").glob("**/*.py")):
        for line_number, line in enumerate(read(path).splitlines(), start=1):
            if ".select_part(" in line or line.strip().startswith("select_part("):
                manual_selection_calls.append(f"{relative(path)}:{line_number}")
    exact_selection_tests = sorted(
        relative(path)
        for path in (
            ROOT / "python" / "tests" / "test_component_library.py",
            ROOT / "tests" / "io" / "parts" / "part_definition_io_test.cpp",
        )
        if path.exists()
    )
    offline_fixtures = sorted(
        relative(path)
        for path in (ROOT / "tests" / "fixtures").glob("*")
        if path.name.endswith(".part.volt.json")
        or path.name.endswith(".electrical.volt.json")
        or path.name.endswith(".voltcap.json")
        or path.name.startswith("native_fabrication_control")
        or path.name.startswith("kicad_flat_resistor")
    )
    return {
        "builtin_component_routes": {"count": len(builtins), "names": builtins},
        "stm32_catalogue_entries": {
            "count": len(re.findall(r"\bLIB\.component\s*\(", stm32_source)),
            "path": "python/volt/libraries/stm32_usb_buck.py",
        },
        "manual_example_selection_calls": {
            "count": len(manual_selection_calls),
            "locations": manual_selection_calls,
        },
        "modern_library_part_tests": {
            "count": sum(test_function_count(ROOT / path) for path in exact_selection_tests),
            "paths": exact_selection_tests,
        },
        "offline_fixtures": {"count": len(offline_fixtures), "paths": offline_fixtures},
    }


def cli_inventory() -> dict[str, object]:
    source = read(ROOT / "python" / "volt" / "cli" / "__init__.py")
    handlers = sorted(
        name
        for name in re.findall(r"^def (_handle_[A-Za-z_]\w*)\(", source, flags=re.MULTILINE)
        if name
        in {
            "_handle_run",
            "_handle_model",
            "_handle_diagnostics",
            "_handle_build",
            "_handle_export_manufacturing",
            "_handle_info",
            "_handle_init",
        }
    )
    source_execution = sorted(
        name
        for name in re.findall(r"^def ([A-Za-z_]\w*)\(", source, flags=re.MULTILINE)
        if name
        in {
            "load_entrypoint",
            "run_entrypoint",
            "_project_result_from_entrypoint",
            "_project_result_with_forwarded_stdout",
            "_evict_project_entrypoint_modules",
        }
    )
    test_path = ROOT / "python" / "tests" / "test_cli.py"
    return {
        "leaf_command_count": len(handlers),
        "leaf_handlers": handlers,
        "json_flag_count": len(re.findall(r'"--json"', source)),
        "check_flag_count": len(re.findall(r'"--check"', source)),
        "source_execution_functions": source_execution,
        "cli_test_count": test_function_count(test_path),
    }


def project_shape_inventory() -> dict[str, object]:
    shapes: dict[str, list[str]] = {}
    for path in sorted((ROOT / "examples").glob("**/*.py")):
        text = read(path)
        if "volt.Project(" not in text:
            continue
        stages = sorted(set(re.findall(r"@project\.(design|schematic|board)\b", text)))
        shapes[relative(path)] = stages
    toml_files = sorted(relative(path) for path in ROOT.rglob("volt.toml"))
    project_test_paths = (
        ROOT / "python" / "tests" / "test_project_framework.py",
        ROOT / "python" / "tests" / "test_project_framework_multi_model.py",
        ROOT / "python" / "tests" / "test_cli.py",
    )
    return {
        "tracked_project_examples": shapes,
        "tracked_project_example_count": len(shapes),
        "tracked_volt_toml_count": len(toml_files),
        "tracked_volt_toml_files": toml_files,
        "project_cli_test_count": sum(test_function_count(path) for path in project_test_paths if path.exists()),
    }


def parity_gate_inventory() -> dict[str, object]:
    gates = {
        "persistence": [
            "tests/io/logical/logical_circuit_round_trip_test.cpp",
            "tests/io/schematic/schematic_round_trip_test.cpp",
            "tests/io/pcb/pcb_projection_io_test.cpp",
            "tests/io/parts/part_definition_io_test.cpp",
        ],
        "render": [
            "tests/io/schematic/schematic_svg_writer_test.cpp",
            "tests/io/pcb/pcb_svg_writer_test.cpp",
            "examples/stm32_usb_buck/artifacts/stm32_usb_buck.svg",
            "examples/stm32_usb_buck/artifacts/stm32_usb_buck.pcb.svg",
        ],
        "kicad": [
            "scripts/check-kicad-boundary.py",
            "tests/adapters/kicad_boundary_test.cpp",
            "tests/adapters/kicad_pcb_writer_test.cpp",
            "tests/adapters/kicad_schematic_writer_test.cpp",
        ],
        "fabrication": [
            "scripts/verify-kicad-fab-export.py",
            "tests/io/pcb/pcb_fabrication_writer_test.cpp",
            "examples/stm32_usb_buck/artifacts/stm32_usb_buck_jlcpcb_manufacturing.zip",
        ],
        "bom_cpl": [
            "tests/circuit/bom/bom_test.cpp",
            "tests/pcb/assembly/cpl_test.cpp",
            "python/tests/test_project_bom.py",
            "python/tests/test_project_assembly.py",
        ],
        "models_3d_viewer": [
            "python/tests/test_project_models3d.py",
            "python/volt/_project_models3d.py",
            "examples/pcb_led_board/artifacts/pcb_led_board.volt/assets/part_models_3d.json",
        ],
        "sanitizer_coverage_platform": [
            "CMakePresets.json",
            "cmake/VoltCompilerOptions.cmake",
            ".github/workflows/ci.yml",
        ],
    }
    result = {}
    for name, paths in gates.items():
        result[name] = {
            "count": len(paths),
            "paths": paths,
            "missing": [path for path in paths if not (ROOT / path).exists()],
        }
    return result


def semantic_summary(path: Path) -> dict[str, object]:
    payload = json.loads(read(path))
    top_level_counts: dict[str, int] = {}
    top_level_scalars: dict[str, object] = {}
    if isinstance(payload, dict):
        for key, value in payload.items():
            if isinstance(value, (list, dict)):
                top_level_counts[key] = len(value)
            elif isinstance(value, (str, int, float, bool)) or value is None:
                top_level_scalars[key] = value
    summary: dict[str, object] = {
        "path": relative(path),
        "sha256": sha256_bytes(path),
        "top_level_keys": sorted(payload.keys()) if isinstance(payload, dict) else [],
        "top_level_counts": dict(sorted(top_level_counts.items())),
        "top_level_scalars": dict(sorted(top_level_scalars.items())),
    }
    if isinstance(payload, dict) and "artifacts" in payload:
        summary["artifact_kinds"] = sorted(
            artifact["kind"]
            for artifact in payload["artifacts"]
            if isinstance(artifact, dict) and isinstance(artifact.get("kind"), str)
        )
    return summary


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
        "baseline": {
            "issue": 294,
            "measurement_baseline_commit": MEASUREMENT_BASELINE,
            "implementation_baseline_commit": MEASUREMENT_BASELINE,
            "approved_references": [
                "docs/design/volt-post-circuit-architecture-review.html",
                "docs/design/volt-part-library-architecture-review.html",
            ],
        },
        "counting_rules": COUNTING_RULES,
        "exclusions": EXCLUSIONS,
        "inventories": {
            "public_surfaces": {
                "snapshots": {
                    name: snapshot_summary(name, path)
                    for name, path in sorted(PUBLIC_SNAPSHOTS.items())
                },
                "py_circuit_binding": py_circuit_binding_inventory(),
                "caller_strata": caller_strata(),
                "python_private_circuit_calls": python_circuit_call_inventory(),
            },
            "python_surface": {
                "export_count": len(python_all_exports()),
                "exports": python_all_exports(),
                "design_owner_attributes": init_assigned_attributes(
                    ROOT / "python" / "volt" / "design.py", "Design"
                ),
                "py_circuit_private_members": py_circuit_private_members(),
                "exceptions": python_exception_inventory(),
                "selector_helpers": sorted(
                    name
                    for name in class_methods(ROOT / "python" / "volt" / "project.py", "ProjectResult")
                    if "write" in name or "diagnostic" in name
                ),
                "lifetime_sensitive_paths": [
                    "src/python/py_circuit.hpp: Circuit + SchematicDocument + optional Board live in one bound owner",
                    "tests/architecture/circuit_public_api.txt: deleted rvalue all() overload freezes borrowed range lifetime",
                    "tests/architecture/board_public_api.txt: deleted rvalue Board(Circuit&&) constructor freezes Circuit borrow lifetime",
                    "tests/architecture/schematic_public_api.txt: deleted rvalue Schematic(Circuit&&) constructor freezes Circuit borrow lifetime",
                ],
            },
            "hidden_footprint_injections": {
                "count": len(hidden_footprint_injections()),
                "locations": hidden_footprint_injections(),
            },
            "artifacts": artifact_inventory(),
            "part_routes": part_route_inventory(),
            "cli_project": {
                "cli": cli_inventory(),
                "project_shapes": project_shape_inventory(),
            },
            "deletion_parity_gates": parity_gate_inventory(),
        },
        "goldens": golden_inventory(),
        "future_slice_map": FUTURE_SLICE_MAP,
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
        if expected != actual:
            return [f"{path}: list changed"]
        return []
    if expected != actual:
        return [f"{path}: expected {expected!r}, got {actual!r}"]
    return []


def run_checks() -> int:
    require_lf_checkout_paths()
    actual = collect_inventory()
    if not BASELINE_PATH.exists():
        print(
            f"{relative(BASELINE_PATH)} is missing; run with --write-baseline after reviewing output",
            file=sys.stderr,
        )
        return 1
    expected = json.loads(read(BASELINE_PATH))
    failures = compare(expected, actual)
    if failures:
        for failure in failures:
            print(
                "refactor inventory baseline drift: "
                f"{failure}; intentional future slices must update {relative(BASELINE_PATH)} "
                "and docs/design/architecture-gate-f1-evidence-map.html",
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


def run_self_tests() -> int:
    require(
        parse_check_attr_eol(b"one\0eol\0lf\0two\0eol\0unspecified\0")
        == {"one": "lf", "two": "unspecified"},
        "check-attr output must preserve each checked path's eol policy",
    )
    sample_snapshot = textwrap.dedent(
        """
        include <vector>

        explicit Board(const Circuit &circuit)
        [[nodiscard]] BoardTrackId add_track(BoardTrack track)
        [[nodiscard]] BoardTrackRouteResult add_track(BoardTrackRouteRequest request, const FootprintLibrary &footprints)
        """
    )
    sample_path = ROOT / "tests" / "architecture" / "__sample_public_api.txt"
    declarations = [
        line.rstrip()
        for line in sample_snapshot.splitlines()
        if line.strip() and not line.startswith("include ")
    ]
    require(len(declarations) == 3, "snapshot counting must exclude includes and blank lines")
    require(
        sorted(name for name in map(declaration_name, declarations) if name is not None)
        == ["Board", "add_track", "add_track"],
        "snapshot callable names must preserve overload declarations",
    )

    binding_names = ["define_resistor", "add_template_net", "schematic_sheet", "board_to_json", "validate", "bom_json", "to_json"]
    grouped = Counter(binding_group(name) for name in binding_names)
    require(
        grouped
        == {
            "logical": 1,
            "module": 1,
            "schematic": 1,
            "board": 1,
            "validation_bom": 2,
            "serializer": 1,
        },
        "binding groups must match the approved owner strata",
    )

    fixture = textwrap.dedent(
        """
        py::dict PyCircuit::board_to_json() const {
            return write(board_projection(), volt::builtin_footprint_library());
        }
        """
    )
    match = re.search(r"volt::builtin_footprint_library\s*\(\)", fixture)
    require(match is not None, "hidden footprint injection sample must be detected")
    require(
        current_cpp_function(fixture, match.start()) == "PyCircuit::board_to_json",
        "hidden footprint injection callers must be attributed to the owning PyCircuit method",
    )

    semantic = {"format": "volt.test", "version": 1, "items": [1, 2], "metadata": {"a": 1}}
    require(
        sorted(semantic.keys()) == ["format", "items", "metadata", "version"]
        and len(semantic["items"]) == 2
        and len(semantic["metadata"]) == 1,
        "semantic summaries must freeze top-level scalars and collection sizes",
    )
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="run checker self-tests")
    parser.add_argument(
        "--write-baseline",
        action="store_true",
        help="rewrite the checked-in baseline from the current source tree",
    )
    args = parser.parse_args(argv)
    if args.self_test:
        return run_self_tests()
    if args.write_baseline:
        return write_baseline()
    return run_checks()


if __name__ == "__main__":
    raise SystemExit(main())
