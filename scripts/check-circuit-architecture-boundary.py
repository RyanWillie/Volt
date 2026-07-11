#!/usr/bin/env python3
"""Check Volt kernel architecture boundaries that should not regress."""

from __future__ import annotations

import argparse
import ast
import re
import sys
import textwrap
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CODE_DIRS = [ROOT / "include", ROOT / "src", ROOT / "tests", ROOT / "examples", ROOT / "python"]
ARCHITECTURE_CODE_DIRS = [ROOT / "include", ROOT / "src"]
ALLOWLIST_DIR = ROOT / "tests" / "architecture"

ROOT_TYPES = {
    "Circuit": ROOT / "include" / "volt" / "circuit" / "circuit.hpp",
    "Board": ROOT / "include" / "volt" / "pcb" / "board.hpp",
    "BoardSpatialIndex": (
        ROOT / "include" / "volt" / "pcb" / "routing" / "board_spatial_index.hpp"
    ),
    "BoardRouter": ROOT / "include" / "volt" / "pcb" / "routing" / "board_router.hpp",
    "Schematic": ROOT / "include" / "volt" / "schematic" / "schematic.hpp",
}
ALLOWED_FLAT_PCB_HEADERS = {
    ROOT / "include" / "volt" / "pcb" / "board.hpp",
}

REJECTED_TOKENS = (
    "CircuitView",
    "CircuitHierarchy",
    "CircuitElectrical",
    "CircuitDesignIntent",
)
FRIEND_TYPE_PREFIXES = ("friend " "class ", "friend " "struct ")

PRIVILEGED_FRIEND_ALLOWLIST = {
    (
        "include/volt/circuit/circuit.hpp",
        "friend void io::detail::restore_logical_connectivity(Circuit &circuit, io::detail::ConnectivityRestoration restoration)",
    ): "The v1 logical reader must restore independent persisted ID tables atomically without exposing raw authoring operations.",
    (
        "include/volt/circuit/circuit.hpp",
        "friend void io::detail::restore_logical_hierarchy(Circuit &circuit, io::detail::HierarchyDefinitionRestoration restoration)",
    ): "The v1 logical reader must restore independent global hierarchy table order without exposing raw authoring operations.",
    (
        "include/volt/circuit/circuit.hpp",
        "friend ModuleInstanceId io::detail::restore_logical_module_instance(Circuit &circuit, io::detail::ModuleInstanceRestoration restoration)",
    ): "The v1 logical reader privately restores persisted module origins after validating their complete relationship set.",
    (
        "include/volt/pcb/routing/board_spatial_index.hpp",
        "friend void detail::validate_copper_clearance(const Board &board, const std::vector<detail::BoardCopperShape> &shapes, DiagnosticReport &report)",
    ): "Board copper DRC currently reuses the spatial index's internal shape snapshot.",
    (
        "include/volt/pcb/routing/board_spatial_index.hpp",
        "friend void detail::insert_after_board_mutation(BoardSpatialIndex &index, BoardSpatialQueryShape shape, std::size_t previous_geometry_mutation_count)",
    ): "BoardRouter mirrors an accepted board mutation into its private runtime spatial index.",
}

CIRCUIT_MUTATOR_PUBLIC_API_SNAPSHOTS = {
    "circuit_connectivity_mutator": (
        "ConnectivityMutator",
        ROOT / "include" / "volt" / "circuit" / "circuit.hpp",
    ),
    "circuit_hierarchy_mutator": (
        "HierarchyMutator",
        ROOT / "include" / "volt" / "circuit" / "circuit.hpp",
    ),
    "circuit_electrical_mutator": (
        "ElectricalMutator",
        ROOT / "include" / "volt" / "circuit" / "circuit.hpp",
    ),
    "circuit_intent_mutator": (
        "IntentMutator",
        ROOT / "include" / "volt" / "circuit" / "circuit.hpp",
    ),
    "circuit_net_class_mutator": (
        "NetClassMutator",
        ROOT / "include" / "volt" / "circuit" / "circuit.hpp",
    ),
}

PYTHON_CONNECTIVITY_SEMANTICS_ALLOWLIST = {}

ENTITY_REF_KERNEL_ALLOWLIST = {
    (
        "src/pcb/copper/board_copper_geometry.cpp",
        "[kind](EntityRef entity) { return entity.kind() == kind; });",
    ): "Existing DRC helper classifies diagnostic copper shapes by reporting refs; keep narrow until a typed shape role replaces it.",
}

KERNEL_MUTATION_ACCESS_TOKENS = ("KernelMutationAccess", "kernel_mutation_access")
# These std surfaces are the structural taxonomy covered by volt/core/errors.hpp.
# std::runtime_error is not a Volt structural kernel-error surface.
RAW_STRUCTURAL_THROW_PATTERN = re.compile(
    r"\bthrow\s+(?:::)?std::(?:logic_error|invalid_argument|out_of_range)\b"
)
PYTHON_BINDING_SOURCE_DIR = ROOT / "src" / "python"
SUBMODEL_STORAGE_ACCESSORS = {"mutable_state", "state"}

SUBMODEL_MUTATORS = {
    "ConnectivityModel": (
        ROOT / "include" / "volt" / "circuit" / "connectivity" / "connectivity_model.hpp",
        {
            "add_pin_definition",
            "add_component_definition",
            "add_component",
            "add_pin",
            "add_net",
            "instantiate_component",
            "connect",
            "disconnect",
            "set_component_property",
        },
    ),
    "ComponentInstance": (
        ROOT / "include" / "volt" / "circuit" / "connectivity" / "instances.hpp",
        {"set_property"},
    ),
    "NetClasses": (
        ROOT / "include" / "volt" / "circuit" / "constraints" / "net_classes.hpp",
        {"add_net_class", "assign_net_class"},
    ),
    "ElectricalModel": (
        ROOT / "include" / "volt" / "circuit" / "electrical" / "electrical_model.hpp",
        {
            "set_component_attribute",
            "set_pin_definition_attribute",
            "set_net_attribute",
            "select_physical_part",
            "set_selected_part_attribute",
        },
    ),
    "PhysicalPart": (
        ROOT / "include" / "volt" / "circuit" / "parts" / "parts.hpp",
        {"set_electrical_attribute"},
    ),
    "DesignIntent": (
        ROOT / "include" / "volt" / "circuit" / "intent" / "design_intent.hpp",
        {
            "mark_intentional_stub_net",
            "mark_intentional_no_connect_pin",
            "set_component_dnp",
            "set_component_selection_override",
        },
    ),
    "ModuleDefinition": (
        ROOT / "include" / "volt" / "circuit" / "hierarchy" / "hierarchy.hpp",
        {"add_template_net", "add_port", "add_component"},
    ),
    "HierarchyModel": (
        ROOT / "include" / "volt" / "circuit" / "hierarchy" / "hierarchy_model.hpp",
        {
            "add_module_definition",
            "add_template_net",
            "add_port_definition",
            "instantiate_root_module",
            "add_module_component",
            "connect_module_pin",
            "restore_root_module_instance",
            "record_module_net_origin",
            "record_module_component_origin",
            "bind_port",
        },
    ),
    "BoardPlacementModel": (
        ROOT / "include" / "volt" / "pcb" / "placement" / "board_placement_model.hpp",
        {"place_component"},
    ),
    "BoardCopperModel": (
        ROOT / "include" / "volt" / "pcb" / "copper" / "board_copper_model.hpp",
        {"add_track", "add_via", "add_zone", "add_keepout", "add_room", "add_text"},
    ),
    "BoardSpatialIndex": (
        ROOT / "include" / "volt" / "pcb" / "routing" / "board_spatial_index.hpp",
        {"insert_after_board_mutation"},
    ),
    "Sheet": (
        ROOT / "include" / "volt" / "schematic" / "schematic_sheet.hpp",
        {
            "add_region",
            "add_symbol_instance",
            "add_wire_run",
            "add_net_label",
            "add_junction",
            "add_power_port",
            "add_no_connect_marker",
            "add_sheet_port",
            "add_symbol_field",
        },
    ),
    "SchematicLibraryModel": (
        ROOT / "include" / "volt" / "schematic" / "schematic_library_model.hpp",
        {"add_symbol_definition"},
    ),
    "SchematicSheetModel": (
        ROOT / "include" / "volt" / "schematic" / "schematic_sheet_model.hpp",
        {
            "add_sheet",
            "add_sheet_region",
            "add_symbol_instance",
            "add_wire_run",
            "add_net_label",
            "add_junction",
            "add_power_port",
            "add_no_connect_marker",
            "add_sheet_port",
            "add_symbol_field",
        },
    ),
    "SchematicItemsModel": (
        ROOT / "include" / "volt" / "schematic" / "schematic_items_model.hpp",
        {
            "move_net_label_text",
            "move_power_port_label",
            "move_symbol_field",
            "add_symbol_instance",
            "add_wire_run",
            "add_net_label",
            "add_junction",
            "add_power_port",
            "add_no_connect_marker",
            "add_sheet_port",
            "add_symbol_field",
        },
    ),
    "BoardStructureModel": (
        ROOT / "include" / "volt" / "pcb" / "structure" / "board_structure_model.hpp",
        {
            "add_layer",
            "set_layer_stack",
            "set_outline",
            "set_design_rules",
            "set_capability_profile",
            "add_feature",
        },
    ),
    "BoardFootprintModel": (
        ROOT / "include" / "volt" / "pcb" / "footprints" / "board_footprint_model.hpp",
        {"cache_footprint_definition"},
    ),
}

SUBMODEL_DERIVATION_ALLOWLIST = {
    ("include/volt/circuit/circuit.hpp", "ConnectivityStorage", "ConnectivityModel"):
        "Circuit-private storage adapter exposes connectivity mutation only to Circuit.",
    ("include/volt/circuit/circuit.hpp", "HierarchyStorage", "HierarchyModel"):
        "Circuit-private storage adapter exposes hierarchy mutation only to Circuit.",
    ("include/volt/circuit/circuit.hpp", "ElectricalStorage", "ElectricalModel"):
        "Circuit-private storage adapter exposes electrical mutation only to Circuit.",
    ("include/volt/circuit/circuit.hpp", "DesignIntentStorage", "DesignIntent"):
        "Circuit-private storage adapter exposes design-intent mutation only to Circuit.",
    ("include/volt/circuit/circuit.hpp", "NetClassStorage", "NetClasses"):
        "Circuit-private storage adapter exposes net-class mutation only to Circuit.",
    ("include/volt/pcb/board.hpp", "StructureStorage", "BoardStructureModel"):
        "Board-private storage adapter exposes board structure mutation only to Board.",
    ("include/volt/pcb/board.hpp", "FootprintStorage", "BoardFootprintModel"):
        "Board-private storage adapter exposes footprint cache mutation only to Board.",
    ("include/volt/pcb/board.hpp", "PlacementStorage", "BoardPlacementModel"):
        "Board-private storage adapter exposes placement mutation only to Board.",
    ("include/volt/pcb/board.hpp", "CopperStorage", "BoardCopperModel"):
        "Board-private storage adapter exposes copper mutation only to Board.",
    ("include/volt/schematic/schematic.hpp", "LibraryStorage", "SchematicLibraryModel"):
        "Schematic-private storage adapter exposes library mutation only to Schematic.",
    ("include/volt/schematic/schematic.hpp", "SheetStorage", "SchematicSheetModel"):
        "Schematic-private storage adapter exposes sheet mutation only to Schematic.",
    ("include/volt/schematic/schematic.hpp", "ItemStorage", "SchematicItemsModel"):
        "Schematic-private storage adapter exposes item mutation only to Schematic.",
    (
        "src/circuit/circuit_storage.hpp",
        "ModuleDefinitionStorage",
        "ModuleDefinition",
    ): "Circuit source-private storage adapter exposes module membership mutation only after root preflight.",
    (
        "src/schematic/schematic_storage.hpp",
        "SheetStorage",
        "Sheet",
    ): "Schematic source-private storage adapter exposes sheet membership mutation only after Schematic preflight.",
    (
        "include/volt/pcb/routing/board_router.hpp",
        "SpatialIndexStorage",
        "BoardSpatialIndex",
    ): "BoardRouter-private storage adapter mirrors committed Board copper into its runtime spatial index.",
}

PRIVATE_STORAGE_HEADER_ALLOWLIST = {
    "src/circuit/circuit_storage.hpp": ("src/circuit/",),
    "src/circuit/subsystem_storage_impl.hpp": ("src/circuit/",),
    "src/pcb/board_storage.hpp": ("src/pcb/",),
    "src/pcb/routing/board_spatial_index_storage.hpp": ("src/pcb/routing/",),
    "src/schematic/schematic_storage.hpp": ("src/schematic/",),
}


@dataclass(frozen=True)
class FriendDeclaration:
    path: Path
    line: int
    declaration: str


@dataclass(frozen=True)
class PythonConnectivityRisk:
    path: Path
    line: int
    qualname: str
    line_text: str
    reason: str


@dataclass(frozen=True)
class SubmodelDerivation:
    path: Path
    line: int
    derived: str
    base: str


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//.*", "", text)


def strip_comments_preserve_lines(text: str) -> str:
    def block_replacement(match: re.Match[str]) -> str:
        return "\n" * match.group(0).count("\n")

    text = re.sub(r"/\*.*?\*/", block_replacement, text, flags=re.DOTALL)
    return re.sub(r"//.*", "", text)


def _blank_preserve_newlines(text: str) -> str:
    return "".join("\n" if char == "\n" else " " for char in text)


def strip_cpp_comments_and_strings_preserve_lines(text: str) -> str:
    output: list[str] = []
    index = 0
    while index < len(text):
        char = text[index]
        next_char = text[index + 1] if index + 1 < len(text) else ""

        if char == "/" and next_char == "/":
            end = text.find("\n", index)
            if end == -1:
                output.append(" " * (len(text) - index))
                break
            output.append(" " * (end - index))
            output.append("\n")
            index = end + 1
            continue

        if char == "/" and next_char == "*":
            end = text.find("*/", index + 2)
            end = len(text) if end == -1 else end + 2
            output.append(_blank_preserve_newlines(text[index:end]))
            index = end
            continue

        if char == "R" and next_char == '"':
            delimiter_end = text.find("(", index + 2)
            if delimiter_end != -1:
                delimiter = text[index + 2 : delimiter_end]
                terminator = ")" + delimiter + '"'
                end = text.find(terminator, delimiter_end + 1)
                if end != -1:
                    end += len(terminator)
                    output.append(_blank_preserve_newlines(text[index:end]))
                    index = end
                    continue

        if char in {'"', "'"}:
            quote = char
            start = index
            index += 1
            while index < len(text):
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == quote:
                    index += 1
                    break
                index += 1
            output.append(_blank_preserve_newlines(text[start:index]))
            continue

        output.append(char)
        index += 1

    return "".join(output)


def code_files() -> list[Path]:
    files: list[Path] = []
    for directory in CODE_DIRS:
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if path.suffix in {".cpp", ".hpp", ".h", ".py"}:
                files.append(path)
    return sorted(files)


def architecture_code_files() -> list[Path]:
    files: list[Path] = []
    for directory in ARCHITECTURE_CODE_DIRS:
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if path.suffix in {".cpp", ".hpp", ".h"}:
                files.append(path)
    return sorted(files)


def is_python_binding_source(path: Path) -> bool:
    try:
        path.relative_to(PYTHON_BINDING_SOURCE_DIR)
    except ValueError:
        return False
    return True


def source_code_files() -> list[Path]:
    source_dir = ROOT / "src"
    if not source_dir.exists():
        return []
    return sorted(
        path
        for path in source_dir.rglob("*")
        if path.suffix in {".cpp", ".hpp", ".h"} and not is_python_binding_source(path)
    )


def relative(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def fail(message: str, failures: list[str]) -> None:
    failures.append(message)


def class_body(header: str, class_name: str) -> str:
    match = re.search(rf"\bclass\s+{re.escape(class_name)}\b[^{{;]*{{", header)
    if match is None:
        raise ValueError(f"class {class_name} not found")

    index = match.end()
    depth = 1
    while index < len(header) and depth > 0:
        if header[index] == "{":
            depth += 1
        elif header[index] == "}":
            depth -= 1
        index += 1
    if depth != 0:
        raise ValueError(f"class {class_name} body is unbalanced")
    return header[match.end() : index - 1]


def public_section(body: str) -> str:
    public = re.search(r"\bpublic\s*:", body)
    if public is None:
        return ""
    private = re.search(r"\b(private|protected)\s*:", body[public.end() :])
    if private is None:
        return body[public.end() :]
    return body[public.end() : public.end() + private.start()]


def access_sections(body: str) -> list[tuple[str, str]]:
    matches = []
    depth = 0
    access_pattern = re.compile(r"\b(public|protected|private)\s*:")
    index = 0
    while index < len(body):
        match = access_pattern.match(body, index)
        if match is not None and depth == 0:
            matches.append(match)
            index = match.end()
            continue
        if body[index] == "{":
            depth += 1
        elif body[index] == "}":
            depth = max(depth - 1, 0)
        index += 1
    if not matches:
        return [("private", body)]

    sections: list[tuple[str, str]] = []
    first = matches[0]
    if first.start() > 0:
        sections.append(("private", body[: first.start()]))
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(body)
        sections.append((match.group(1), body[match.end() : end]))
    return sections


def strip_nested_type_definitions(section: str) -> str:
    output = list(section)
    pattern = re.compile(r"\b(?:class|struct)\s+[A-Za-z_]\w*\b")
    index = 0
    while True:
        match = pattern.search(section, index)
        if match is None:
            break

        brace = section.find("{", match.end())
        semicolon = section.find(";", match.end())
        if brace == -1 or (semicolon != -1 and semicolon < brace):
            index = match.end()
            continue

        depth = 1
        cursor = brace + 1
        while cursor < len(section) and depth > 0:
            if section[cursor] == "{":
                depth += 1
            elif section[cursor] == "}":
                depth -= 1
            cursor += 1
        if depth != 0:
            index = match.end()
            continue
        while cursor < len(section) and section[cursor].isspace():
            cursor += 1
        if cursor < len(section) and section[cursor] == ";":
            cursor += 1

        for replace_index in range(match.start(), cursor):
            output[replace_index] = "\n" if section[replace_index] == "\n" else " "
        index = cursor

    return "".join(output)


def normalize_declaration(declaration: str) -> str:
    declaration = re.sub(r"\{.*\}\s*$", "", declaration, flags=re.DOTALL)
    declaration = declaration.rstrip(";")
    declaration = re.sub(r"\s+", " ", declaration)
    declaration = re.sub(r"\(\s+", "(", declaration)
    declaration = re.sub(r"\s+\)", ")", declaration)
    declaration = re.sub(r"\s+,", ",", declaration)
    declaration = re.sub(r"\s*([*&])\s+([A-Za-z_]\w*)", r" \1\2", declaration)
    return declaration.strip()


def friend_declarations(path: Path, text: str) -> list[FriendDeclaration]:
    declarations: list[FriendDeclaration] = []
    pending: list[str] | None = None
    pending_line = 0
    brace_depth = 0
    stripped = strip_comments_preserve_lines(text)
    for line_number, line in enumerate(stripped.splitlines(), start=1):
        if pending is None:
            match = re.search(r"\bfriend\b", line)
            if match is None:
                continue
            pending = [line[match.start() :]]
            pending_line = line_number
            brace_depth = 0
        else:
            pending.append(line.strip())

        chunk = " ".join(pending)
        brace_depth += line.count("{") - line.count("}")
        if "{" in chunk:
            if brace_depth > 0:
                continue
        elif ";" not in chunk:
            continue
        declarations.append(
            FriendDeclaration(path=path, line=pending_line, declaration=normalize_declaration(chunk))
        )
        pending = None
        pending_line = 0
        brace_depth = 0
    return declarations


def is_comparison_operator_friend(declaration: str) -> bool:
    return re.search(r"\bfriend\b.*\boperator\s*(==|!=|<=>|<=|>=|<|>)\s*\(", declaration) is not None


def is_broad_type_friend(declaration: str) -> bool:
    return declaration.startswith(FRIEND_TYPE_PREFIXES)


def is_privileged_friend(declaration: str) -> bool:
    return declaration.startswith("friend ") and not is_comparison_operator_friend(declaration)


def declaration_chunks(section: str) -> list[str]:
    section = re.sub(r"(\)\s*(?:const\s*)?(?:noexcept\s*)?)\{[^{}]*\}", r"\1;", section)
    return [normalize_declaration(chunk) for chunk in section.split(";")]


def public_declarations(class_name: str, header_path: Path) -> list[str]:
    return public_declarations_from_header(class_name, read(header_path))


def public_declarations_from_header(class_name: str, header: str) -> list[str]:
    body = class_body(strip_comments(header), class_name)
    return declarations_from_accesses(body, {"public"})


def declarations_from_accesses(body: str, access_names: set[str]) -> list[str]:
    declarations: list[str] = []
    for access, section in access_sections(body):
        if access not in access_names:
            continue
        for chunk in declaration_chunks(strip_nested_type_definitions(section)):
            if "(" not in chunk or ")" not in chunk:
                continue
            name = declaration_function_name(chunk)
            if name is None:
                continue
            if name in {"if", "for", "while", "switch"}:
                continue
            declarations.append(chunk)
    return declarations


def public_or_protected_declarations(class_name: str, header_path: Path) -> list[tuple[str, str]]:
    body = class_body(strip_comments(read(header_path)), class_name)
    declarations: list[tuple[str, str]] = []
    for access, section in access_sections(body):
        if access not in {"public", "protected"}:
            continue
        for chunk in declaration_chunks(section):
            if "(" not in chunk or ")" not in chunk:
                continue
            name = declaration_function_name(chunk)
            if name is None:
                continue
            if name in {"if", "for", "while", "switch"}:
                continue
            declarations.append((access, chunk))
    return declarations


def declaration_function_name(declaration: str) -> str | None:
    prefix = declaration.split("(", 1)[0].strip()
    if not prefix:
        return None
    return prefix.split()[-1].split("::")[-1].strip("&*")


def submodel_derivations(path: Path, text: str) -> list[SubmodelDerivation]:
    stripped = strip_comments_preserve_lines(text)
    derivations: list[SubmodelDerivation] = []
    pattern = re.compile(
        r"\b(?:class|struct)\s+([A-Za-z_]\w*)\s*(?:final\s*)?:\s*([^{;]+)\{"
    )
    for match in pattern.finditer(stripped):
        derived = match.group(1)
        base_clause = match.group(2)
        # Scan every identifier in the base clause, including template arguments, so
        # indirect derivations such as SubsystemStorage<ConnectivityModel, State> still
        # count as deriving from the mutating submodel facade.
        seen_bases: set[str] = set()
        for base_match in re.finditer(r"\b([A-Za-z_]\w*)\b", base_clause):
            base = base_match.group(1)
            if base not in SUBMODEL_MUTATORS or base in seen_bases:
                continue
            seen_bases.add(base)
            derivations.append(
                SubmodelDerivation(
                    path=path,
                    line=stripped.count("\n", 0, match.start()) + 1,
                    derived=derived,
                    base=base,
                )
            )
    return derivations


def header_includes(header_path: Path) -> list[str]:
    includes: list[str] = []
    for line in read(header_path).splitlines():
        match = re.match(r"\s*#include\s+([<\"].*[>\"])", line)
        if match is not None:
            includes.append(f"include {match.group(1)}")
    return includes


def public_api_snapshot(class_name: str, header_path: Path) -> list[str]:
    return [
        *header_includes(header_path),
        "",
        *public_declarations(class_name, header_path),
    ]


def python_pcb_authoring_files() -> list[Path]:
    python_dir = ROOT / "python" / "volt"
    if not python_dir.exists():
        return []
    return sorted(
        path
        for path in python_dir.glob("_pcb*.py")
        if path.name not in {"_pcb_models.py"}
    )


def call_name(node: ast.AST) -> str | None:
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        return node.attr
    return None


def names_in(node: ast.AST) -> set[str]:
    return {child.id for child in ast.walk(node) if isinstance(child, ast.Name)}


class _PythonConnectivityRiskVisitor(ast.NodeVisitor):
    def __init__(self) -> None:
        self.risks: set[tuple[int, str]] = set()

    def visit_Call(self, node: ast.Call) -> None:
        name = call_name(node.func)
        if name in {"pad_net", "_pad_anchor_net"}:
            self.risks.add((node.lineno, "uses Python pad-anchor net lookup"))
        self.generic_visit(node)

    def visit_Compare(self, node: ast.Compare) -> None:
        compared_names = names_in(node.left)
        for comparator in node.comparators:
            compared_names.update(names_in(comparator))
        if any(name.endswith("_net") for name in compared_names):
            self.risks.add((node.lineno, "compares route-net identity in Python"))
        self.generic_visit(node)

    def visit_Return(self, node: ast.Return) -> None:
        if isinstance(node.value, ast.Attribute) and node.value.attr == "net":
            self.risks.add((node.lineno, "returns net identity from Python object state"))
        self.generic_visit(node)


def function_qualname(stack: list[str], name: str) -> str:
    return ".".join([*stack, name])


def python_connectivity_risks(path: Path, text: str) -> list[PythonConnectivityRisk]:
    tree = ast.parse(text, filename=relative(path))
    lines = text.splitlines()
    risks: list[PythonConnectivityRisk] = []

    class Visitor(ast.NodeVisitor):
        def __init__(self) -> None:
            self.stack: list[str] = []

        def visit_ClassDef(self, node: ast.ClassDef) -> None:
            self.stack.append(node.name)
            self.generic_visit(node)
            self.stack.pop()

        def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
            visitor = _PythonConnectivityRiskVisitor()
            for child in node.body:
                visitor.visit(child)
            for line, reason in sorted(visitor.risks):
                risks.append(
                    PythonConnectivityRisk(
                        path=path,
                        line=line,
                        qualname=function_qualname(self.stack, node.name),
                        line_text=lines[line - 1].strip(),
                        reason=reason,
                    )
                )

    Visitor().visit(tree)
    return risks


def entity_ref_sensitive_files() -> list[Path]:
    roots = (
        ROOT / "include" / "volt" / "circuit",
        ROOT / "include" / "volt" / "pcb",
        ROOT / "include" / "volt" / "schematic",
        ROOT / "src" / "circuit",
        ROOT / "src" / "pcb",
        ROOT / "src" / "schematic",
    )
    files: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if path.suffix in {".cpp", ".hpp", ".h"}:
                files.append(path)
    return sorted(files)


def entity_ref_names(text: str) -> set[str]:
    lines = text.splitlines()
    names = set(
        re.findall(
            r"\bEntityRef\s*(?:[&*]\s*)?([A-Za-z_]\w*)",
            text,
        )
    )
    for line in lines:
        names.update(
            re.findall(
                r"\bstd::(?:array|span|vector)\s*<[^;{}]*\bEntityRef\b[^;{}]*>\s*"
                r"(?:[&*]\s*)?([A-Za-z_]\w*)",
                line,
            )
        )
        names.update(
            re.findall(
                r"\bstd::pair\s*<[^;{}]*\bEntityRef\b[^;{}]*>\s*(?:[&*]\s*)?"
                r"([A-Za-z_]\w*)",
                line,
            )
        )
    names.update(
        re.findall(
            r"\bfor\s*\([^:]+?\b([A-Za-z_]\w*)\s*:\s*[^)]*\.entities\s*\(",
            text,
        )
    )
    changed = True
    while changed:
        changed = False
        for line in lines:
            matches = re.findall(
                r"\b(?:const\s+)?auto\s*(?:[&*]\s*)?([A-Za-z_]\w*)\s*=\s*[^;]*"
                r"\.entities\s*\(\)\s*(?:\[[^\]]+\]|\.front\s*\(\)|\.back\s*\(\))",
                line,
            )
            for name in matches:
                if name not in names:
                    names.add(name)
                    changed = True
            for source in tuple(names):
                matches = re.findall(
                    rf"\b(?:const\s+)?auto\s*(?:[&*]\s*)?([A-Za-z_]\w*)\s*=\s*"
                    rf"{re.escape(source)}\s*(?:\[[^\]]+\]|\.front\s*\(\)|\.back\s*\(\))",
                    line,
                )
                for name in matches:
                    if name not in names:
                        names.add(name)
                        changed = True
    return names


def entity_ref_traversal_lines(text: str) -> list[tuple[int, str]]:
    stripped = strip_comments_preserve_lines(text)
    names = entity_ref_names(stripped)
    lines: list[tuple[int, str]] = []
    for line_number, line in enumerate(stripped.splitlines(), start=1):
        if re.search(
            r"\.entities\s*\(\)\s*(?:\[[^\]]+\]|\.front\(\)|\.back\(\))\s*"
            r"\.\s*(kind|index)\s*\(",
            line,
        ):
            lines.append((line_number, line.strip()))
            continue
        for name in names:
            if re.search(rf"\b{re.escape(name)}\s*\.\s*(kind|index)\s*\(", line):
                lines.append((line_number, line.strip()))
                break
            if re.search(rf"\b{re.escape(name)}\s*(?:\[[^\]]+\]|\.front\(\)|\.back\(\))\s*\.\s*(kind|index)\s*\(", line):
                lines.append((line_number, line.strip()))
                break
            if re.search(rf"\b{re.escape(name)}\s*\.\s*(?:first|second)\s*\.\s*(kind|index)\s*\(", line):
                lines.append((line_number, line.strip()))
                break
    return lines


def raw_structural_throw_lines(text: str) -> list[tuple[int, str]]:
    stripped = strip_cpp_comments_and_strings_preserve_lines(text)
    original_lines = text.splitlines()
    lines: list[tuple[int, str]] = []
    for match in RAW_STRUCTURAL_THROW_PATTERN.finditer(stripped):
        line_number = stripped.count("\n", 0, match.start()) + 1
        lines.append((line_number, original_lines[line_number - 1].strip()))
    return lines


def check_rejected_tokens(failures: list[str]) -> None:
    for path in code_files():
        text = read(path)
        for token in REJECTED_TOKENS:
            if token in text:
                fail(f"{relative(path)} contains rejected architecture token {token}", failures)


def check_no_kernel_mutation_access(failures: list[str]) -> None:
    for path in architecture_code_files():
        text = read(path)
        for token in KERNEL_MUTATION_ACCESS_TOKENS:
            if token in text:
                fail(
                    f"{relative(path)} still references broad kernel mutation passkey token "
                    f"{token}",
                    failures,
                )


def check_no_broad_type_friends(failures: list[str]) -> None:
    for path in architecture_code_files():
        for declaration in friend_declarations(path, read(path)):
            if is_broad_type_friend(declaration.declaration):
                fail(
                    f"{relative(path)}:{declaration.line} declares broad type friendship "
                    "instead of a narrow API",
                    failures,
                )


def check_no_flat_pcb_public_headers(failures: list[str]) -> None:
    pcb_dir = ROOT / "include" / "volt" / "pcb"
    for path in sorted(pcb_dir.glob("*.hpp")):
        if path not in ALLOWED_FLAT_PCB_HEADERS:
            fail(
                f"{path.relative_to(ROOT)} is a flat PCB public header; use an ownership-folder path instead",
                failures,
            )


def check_privileged_friends_are_allowlisted(failures: list[str]) -> None:
    found_allowlisted: set[tuple[str, str]] = set()
    for path in architecture_code_files():
        for declaration in friend_declarations(path, read(path)):
            if not is_privileged_friend(declaration.declaration):
                continue
            if is_broad_type_friend(declaration.declaration):
                continue
            key = (relative(path), declaration.declaration)
            if key in PRIVILEGED_FRIEND_ALLOWLIST:
                found_allowlisted.add(key)
                continue
            fail(
                f"{relative(path)}:{declaration.line} declares non-comparison friend "
                f"{declaration.declaration!r} without a documented architecture allowlist reason",
                failures,
            )

    for key, reason in sorted(PRIVILEGED_FRIEND_ALLOWLIST.items()):
        if not reason.strip():
            fail(f"privileged friend allowlist entry {key!r} must document a reason", failures)
        if key not in found_allowlisted:
            fail(f"privileged friend allowlist entry {key!r} no longer matches source", failures)


def check_public_api_snapshots(failures: list[str]) -> None:
    snapshots = {
        **{class_name.lower(): (class_name, header_path)
           for class_name, header_path in ROOT_TYPES.items()},
        **CIRCUIT_MUTATOR_PUBLIC_API_SNAPSHOTS,
    }
    for snapshot_name, (class_name, header_path) in snapshots.items():
        allowlist = ALLOWLIST_DIR / f"{snapshot_name}_public_api.txt"
        if not allowlist.exists():
            fail(f"{relative(allowlist)} is missing", failures)
            continue

        actual = public_api_snapshot(class_name, header_path)
        expected = [
            line.rstrip()
            for line in read(allowlist).splitlines()
            if not line.startswith("#")
        ]
        if actual != expected:
            fail(
                f"{relative(allowlist)} does not match {relative(header_path)}",
                failures,
            )


def check_python_connectivity_semantics(failures: list[str]) -> None:
    found_allowlisted: set[tuple[str, str, str]] = set()
    for path in python_pcb_authoring_files():
        for risk in python_connectivity_risks(path, read(path)):
            key = (relative(path), risk.qualname, risk.line_text)
            if key in PYTHON_CONNECTIVITY_SEMANTICS_ALLOWLIST:
                found_allowlisted.add(key)
                continue
            fail(
                f"{relative(path)}:{risk.line} function {risk.qualname} owns PCB "
                f"connectivity/net inference in Python ({risk.reason}): {risk.line_text}",
                failures,
            )

    for key, reason in sorted(PYTHON_CONNECTIVITY_SEMANTICS_ALLOWLIST.items()):
        if not reason.strip():
            fail(f"Python connectivity allowlist entry {key!r} must document a reason", failures)
        if key not in found_allowlisted:
            fail(f"Python connectivity allowlist entry {key!r} no longer matches source", failures)


def check_entity_ref_not_kernel_traversal_handle(failures: list[str]) -> None:
    found_allowlisted: set[tuple[str, str]] = set()
    for path in entity_ref_sensitive_files():
        for line_number, line in entity_ref_traversal_lines(read(path)):
            key = (relative(path), line)
            if key in ENTITY_REF_KERNEL_ALLOWLIST:
                found_allowlisted.add(key)
                continue
            fail(
                f"{relative(path)}:{line_number} branches on or unwraps EntityRef in a "
                f"kernel-owned layer: {line}",
                failures,
            )

    for key, reason in sorted(ENTITY_REF_KERNEL_ALLOWLIST.items()):
        if not reason.strip():
            fail(f"EntityRef kernel allowlist entry {key!r} must document a reason", failures)
        if key not in found_allowlisted:
            fail(f"EntityRef kernel allowlist entry {key!r} no longer matches source", failures)


def check_no_raw_structural_throws(failures: list[str]) -> None:
    for path in source_code_files():
        for line_number, line in raw_structural_throw_lines(read(path)):
            fail(
                f"{relative(path)}:{line_number} throws raw structural std exception; "
                f"use a typed volt::Kernel*Error with an ErrorCode: {line}",
                failures,
            )


def subsystem_root_name(path: Path) -> str | None:
    name = path.name
    if path.match("include/volt/circuit/*.hpp") or path.match("include/volt/circuit/**/*.hpp"):
        if name in {"circuit.hpp", "validation.hpp", "queries.hpp"}:
            return None
        if "_model" in name or name in {"design_intent.hpp", "net_classes.hpp"}:
            return "Circuit"
    if path.match("include/volt/pcb/**/*.hpp") or path.match("include/volt/pcb/*.hpp"):
        if name == "board.hpp":
            return None
        if "_model" in name:
            return "Board"
    if path.match("include/volt/schematic/*"):
        if name == "schematic.hpp":
            return None
        if "_model" in name:
            return "Schematic"
    return None


def check_subsystem_back_references(failures: list[str]) -> None:
    for path in code_files():
        root_name = subsystem_root_name(path.relative_to(ROOT))
        if root_name is None:
            continue
        text = strip_comments(read(path))
        if re.search(rf"\b(?:const\s+)?{root_name}\s*[&*]", text):
            fail(f"{relative(path)} holds or accepts a {root_name} back-reference", failures)


def check_subsystem_sources_have_real_logic(failures: list[str]) -> None:
    source_patterns = [
        ROOT / "src" / "circuit",
        ROOT / "src" / "pcb",
        ROOT / "src" / "schematic",
    ]
    for directory in source_patterns:
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if path.suffix != ".cpp":
                continue
            name = path.name
            if "_model" not in name and name not in {
                "design_intent.cpp",
                "net_classes.cpp",
            }:
                continue
            text = strip_comments(read(path))
            if re.search(r"\b(?:circuit_|board_|schematic_|root_)\s*(?:\.|->)", text):
                fail(f"{relative(path)} delegates through a model root back-reference", failures)
            meaningful_lines = [
                line.strip()
                for line in text.splitlines()
                if line.strip()
                and not line.strip().startswith("#include")
                and line.strip() not in {"namespace volt {", "} // namespace volt", "}"}
            ]
            if len(meaningful_lines) < 4:
                fail(f"{relative(path)} does not contain enough real subsystem logic", failures)


def check_no_public_submodel_mutators(failures: list[str]) -> None:
    for class_name, (header_path, method_names) in sorted(SUBMODEL_MUTATORS.items()):
        for access, declaration in public_or_protected_declarations(class_name, header_path):
            if declaration_function_name(declaration) not in method_names:
                continue
            fail(
                f"{relative(header_path)} exposes {access} submodel mutation API "
                f"{class_name}::{declaration_function_name(declaration)}; use the owning "
                "aggregate root command surface",
                failures,
            )


def check_no_public_submodel_storage_accessors(failures: list[str]) -> None:
    for class_name, (header_path, _) in sorted(SUBMODEL_MUTATORS.items()):
        for access, declaration in public_or_protected_declarations(class_name, header_path):
            function_name = declaration_function_name(declaration)
            if function_name not in SUBMODEL_STORAGE_ACCESSORS:
                continue
            fail(
                f"{relative(header_path)} exposes {access} submodel storage accessor "
                f"{class_name}::{function_name}; composed storage must stay behind the "
                "owning aggregate root implementation boundary",
                failures,
            )


def check_private_storage_headers_stay_private(failures: list[str]) -> None:
    header_names = {
        Path(path).name: (path, allowed_prefixes)
        for path, allowed_prefixes in PRIVATE_STORAGE_HEADER_ALLOWLIST.items()
    }
    found_headers: set[str] = set()
    include_pattern = re.compile(r'^\s*#\s*include\s+[<"]([^>"]+)[>"]')
    for path in architecture_code_files():
        if path.suffix not in {".cpp", ".hpp", ".h"}:
            continue
        rel = relative(path)
        for line_number, line in enumerate(read(path).splitlines(), start=1):
            match = include_pattern.match(line)
            if match is None:
                continue
            include_name = Path(match.group(1)).name
            if include_name not in header_names:
                continue
            header_path, allowed_prefixes = header_names[include_name]
            found_headers.add(header_path)
            if not any(rel.startswith(prefix) for prefix in allowed_prefixes):
                fail(
                    f"{rel}:{line_number} includes private storage header {header_path} "
                    "outside its owning implementation directory",
                    failures,
                )

    for header_path in sorted(PRIVATE_STORAGE_HEADER_ALLOWLIST):
        if not (ROOT / header_path).exists():
            fail(f"private storage header {header_path} is missing", failures)
        elif header_path not in found_headers:
            fail(f"private storage header {header_path} is not included by its owner", failures)


def check_no_submodel_derivation_escape_hatches(failures: list[str]) -> None:
    found_allowlisted: set[tuple[str, str, str]] = set()
    for path in code_files():
        if path.suffix not in {".cpp", ".hpp", ".h"}:
            continue
        for derivation in submodel_derivations(path, read(path)):
            key = (relative(derivation.path), derivation.derived, derivation.base)
            if key in SUBMODEL_DERIVATION_ALLOWLIST:
                found_allowlisted.add(key)
                continue
            fail(
                f"{relative(derivation.path)}:{derivation.line} derives "
                f"{derivation.derived} from mutating submodel {derivation.base}; this can "
                "re-export protected storage mutators outside the owning aggregate root",
                failures,
            )

    for key, reason in sorted(SUBMODEL_DERIVATION_ALLOWLIST.items()):
        if not reason.strip():
            fail(f"submodel derivation allowlist entry {key!r} must document a reason", failures)
        if key not in found_allowlisted:
            fail(f"submodel derivation allowlist entry {key!r} no longer matches source", failures)


def require_self_test(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_self_tests() -> int:
    friend_class_declaration = FRIEND_TYPE_PREFIXES[0] + "Backdoor;"
    friend_struct_declaration = FRIEND_TYPE_PREFIXES[1] + "StructBackdoor;"
    friend_sample = textwrap.dedent(
        """
        struct Comparable {
            [[nodiscard]] friend bool operator==(const Comparable &lhs,
                                                 const Comparable &rhs) noexcept {
                return lhs.value == rhs.value;
            }
            CLASS_FRIEND_DECLARATION
            STRUCT_FRIEND_DECLARATION
            friend void mutate_private_state(Comparable &value);
            int value = 0;
        };
        """
    ).replace("CLASS_FRIEND_DECLARATION", friend_class_declaration).replace(
        "STRUCT_FRIEND_DECLARATION", friend_struct_declaration
    )
    friends = friend_declarations(Path("sample.hpp"), friend_sample)
    require_self_test(
        any(is_comparison_operator_friend(item.declaration) for item in friends),
        "comparison operator friends must be classified as non-privileged value semantics",
    )
    privileged = [item for item in friends if is_privileged_friend(item.declaration)]
    require_self_test(
        sum(is_broad_type_friend(item.declaration) for item in privileged) == 2,
        "type-level friend grants must be identifiable as broad privileged access",
    )
    require_self_test(
        [item.declaration for item in privileged]
        == [
            friend_class_declaration.rstrip(";"),
            friend_struct_declaration.rstrip(";"),
            "friend void mutate_private_state(Comparable &value)",
        ],
        "non-operator friend grants must be classified as privileged access",
    )
    inline_privileged_friend = textwrap.dedent(
        """
        struct Sneaky {
            friend void mutate_private_state(Sneaky &value) {
                if (operator==(value, value)) {
                    value.secret = 1;
                }
            }
            [[nodiscard]] friend bool operator==(const Sneaky &lhs,
                                                 const Sneaky &rhs) noexcept = default;
            int secret = 0;
        };
        """
    )
    inline_friends = friend_declarations(Path("sample.hpp"), inline_privileged_friend)
    require_self_test(
        any(item.declaration == "friend void mutate_private_state(Sneaky &value)"
            and is_privileged_friend(item.declaration)
            for item in inline_friends),
        "inline non-operator friend bodies must not be truncated into comparison friends",
    )

    submodel_mutator_sample = textwrap.dedent(
        """
        class SampleModel {
          public:
            int read_value() const;
            void mutate(int value);

          protected:
            void protected_mutation(int value);

          private:
            void hidden_mutation(int value);
        };
        """
    )
    sample_declarations = public_declarations_from_header("SampleModel", submodel_mutator_sample)
    require_self_test(
        any(
            declaration_function_name(declaration) == "mutate"
            for declaration in sample_declarations
        ),
        "public submodel mutator samples must be observable",
    )
    require_self_test(
        any(declaration_function_name(declaration) == "read_value"
            for declaration in sample_declarations),
        "public submodel read samples must remain observable",
    )
    require_self_test(
        all(declaration_function_name(declaration) != "hidden_mutation"
            for declaration in sample_declarations),
        "private submodel mutators must not be treated as public architecture surface",
    )
    forward_decl_sample = textwrap.dedent(
        """
        class SampleModel;

        namespace detail {
        void helper(SampleModel &model);
        }

        class SampleModel {
          public:
            int read_value() const;
        };
        """
    )
    require_self_test(
        public_declarations_from_header("SampleModel", forward_decl_sample)
        == ["int read_value() const"],
        "public API parser must ignore forward declarations before the real class body",
    )
    nested_public_sample = textwrap.dedent(
        """
        class SampleRoot {
          public:
            class Mutator {
              public:
                void mutate();
            };

            int read_value() const;
        };
        """
    )
    require_self_test(
        public_declarations_from_header("SampleRoot", nested_public_sample)
        == ["int read_value() const"],
        "public API parser must ignore nested facade methods in the root surface",
    )
    require_self_test(
        public_declarations_from_header("Mutator", nested_public_sample) == ["void mutate()"],
        "nested facade methods must remain observable for dedicated facade snapshots",
    )
    # public_or_protected_declarations reads from disk, so exercise the parser directly instead.
    sample_body = class_body(strip_comments(submodel_mutator_sample), "SampleModel")
    protected_sample = [
        declaration
        for access, section in access_sections(sample_body)
        if access == "protected"
        for declaration in declaration_chunks(section)
    ]
    require_self_test(
        any(declaration_function_name(declaration) == "protected_mutation"
            for declaration in protected_sample),
        "protected submodel mutator samples must be observable",
    )
    protected_storage_sample = textwrap.dedent(
        """
        class SampleStorageModel {
          public:
            int read_value() const;

          protected:
            detail::SampleState &mutable_state() noexcept;
            const detail::SampleState &state() const noexcept;
        };
        """
    )
    storage_body = class_body(strip_comments(protected_storage_sample), "SampleStorageModel")
    storage_accessors = [
        declaration
        for access, section in access_sections(storage_body)
        if access in {"public", "protected"}
        for declaration in declaration_chunks(section)
        if declaration_function_name(declaration) in SUBMODEL_STORAGE_ACCESSORS
    ]
    require_self_test(
        {declaration_function_name(declaration) for declaration in storage_accessors}
        == SUBMODEL_STORAGE_ACCESSORS,
        "public/protected submodel storage accessors must be observable",
    )
    submodel_derivation_sample = textwrap.dedent(
        """
        struct TestConnectivityModel : volt::ConnectivityModel {
          public:
            using ConnectivityModel::add_net;
        };
        """
    )
    sample_derivations = submodel_derivations(Path("tests/sample.cpp"), submodel_derivation_sample)
    require_self_test(
        any(
            derivation.derived == "TestConnectivityModel"
            and derivation.base == "ConnectivityModel"
            for derivation in sample_derivations
        ),
        "subclasses of mutating submodels must be reported as derivation escape hatches",
    )

    python_sample = textwrap.dedent(
        """
        def _route_net(context, net, start, end):
            if net is not None:
                return net
            start_net = context.pad_net(start)
            end_net = context.pad_net(end)
            if start_net != end_net:
                raise ValueError("different nets")
            return start_net
        """
    )
    risks = python_connectivity_risks(Path("python/volt/_pcb_new.py"), python_sample)
    require_self_test(
        any(risk.qualname == "_route_net" for risk in risks),
        "Python route-net inference sample must be reported",
    )
    python_allowlist_sample = textwrap.dedent(
        """
        def _route_net(context, net, start, middle, end):
            start_net = context.pad_net(start)
            middle_net = context.pad_net(middle)
            return start_net if middle_net is None else middle_net
        """
    )
    allowlist_risks = python_connectivity_risks(
        Path("python/volt/_pcb_composition.py"), python_allowlist_sample
    )
    unallowlisted = [
        risk
        for risk in allowlist_risks
        if (relative(risk.path), risk.qualname, risk.line_text)
        not in PYTHON_CONNECTIVITY_SEMANTICS_ALLOWLIST
    ]
    require_self_test(
        any("middle_net = context.pad_net(middle)" == risk.line_text for risk in unallowlisted),
        "extra Python route-net inference inside known-debt functions must still fail",
    )
    python_projection_sample = textwrap.dedent(
        """
        def text(context, value, *, at, layer):
            anchor = context.anchor_at(at)
            return context.board.add_text(value, at=anchor.point, layer=layer)
        """
    )
    require_self_test(
        not python_connectivity_risks(Path("python/volt/_pcb_projection.py"), python_projection_sample),
        "Python presentation helpers must not be reported as connectivity ownership",
    )

    raw_structural_throw_sample = textwrap.dedent(
        """
        void reject_bad_state() {
            throw std::logic_error{"raw structural failure"};
        }
        """
    )
    raw_structural_throws = raw_structural_throw_lines(raw_structural_throw_sample)
    require_self_test(
        raw_structural_throws == [(3, "throw std::logic_error{\"raw structural failure\"};")],
        "raw structural std throws in source code must be reported",
    )
    raw_structural_throw_multiline_sample = textwrap.dedent(
        """
        void reject_bad_argument() {
            throw
                ::std::invalid_argument{"raw structural failure"};
        }
        """
    )
    raw_structural_multiline_throws = raw_structural_throw_lines(
        raw_structural_throw_multiline_sample
    )
    require_self_test(
        raw_structural_multiline_throws == [(3, "throw")],
        "raw structural std throw scanner must report multiline and ::std spellings",
    )
    raw_structural_throw_false_positive_sample = textwrap.dedent(
        """
        void document_policy() {
            // throw std::invalid_argument{"documented but not executable"};
            const char *text = "throw std::out_of_range{not executable}";
            const char *raw = R"(throw std::logic_error{not executable})";
        }
        """
    )
    require_self_test(
        not raw_structural_throw_lines(raw_structural_throw_false_positive_sample),
        "raw structural throw scanner must ignore comments and string literals",
    )
    require_self_test(
        is_python_binding_source(ROOT / "src" / "python" / "bindings.cpp"),
        "raw structural throw checker must identify Python binding source",
    )
    require_self_test(
        not is_python_binding_source(ROOT / "src" / "circuit" / "circuit.cpp"),
        "raw structural throw checker must keep non-Python source in scope",
    )

    entity_ref_bad = textwrap.dedent(
        """
        void mutate_from_ref(Circuit &circuit, EntityRef ref) {
            if (ref.kind() != EntityKind::Net) {
                return;
            }
            circuit.net(NetId{ref.index()});
        }
        """
    )
    require_self_test(
        len(entity_ref_traversal_lines(entity_ref_bad)) == 2,
        "EntityRef kind/index use in kernel code must be reported",
    )
    entity_ref_auto_bad = textwrap.dedent(
        """
        void mutate_from_auto_ref(const Diagnostic &diagnostic,
                                  const std::vector<EntityRef> &entities) {
            auto first = diagnostic.entities().front();
            if (first.kind() == EntityKind::Net) {
                const auto copied = entities.front();
                (void)copied.index();
            }
        }
        """
    )
    require_self_test(
        len(entity_ref_traversal_lines(entity_ref_auto_bad)) == 2,
        "EntityRef auto aliases from diagnostic entity lists must be reported",
    )
    entity_ref_container_bad = textwrap.dedent(
        """
        void mutate_from_containers(const Diagnostic &diagnostic,
                                    std::span<const EntityRef> refs,
                                    std::pair<EntityRef, EntityRef> pair) {
            if (diagnostic.entities().front().kind() == EntityKind::Net) {
                return;
            }
            if (refs.front().index() == 0U) {
                return;
            }
            (void)pair.first.kind();
        }
        """
    )
    require_self_test(
        len(entity_ref_traversal_lines(entity_ref_container_bad)) == 3,
        "EntityRef direct, span, and pair access must be reported",
    )
    entity_ref_reporting = textwrap.dedent(
        """
        void add_diagnostic(DiagnosticReport &report, NetId net) {
            report.add(Diagnostic{Severity::Warning, DiagnosticCategory{"erc"},
                                  DiagnosticCode{"net.stub"}, "Stub",
                                  std::vector{EntityRef::net(net)}});
        }
        """
    )
    require_self_test(
        not entity_ref_traversal_lines(entity_ref_reporting),
        "EntityRef diagnostic reporting must stay allowed",
    )

    return 0


def run_checks() -> int:
    failures: list[str] = []
    check_rejected_tokens(failures)
    check_no_kernel_mutation_access(failures)
    check_no_broad_type_friends(failures)
    check_no_flat_pcb_public_headers(failures)
    check_privileged_friends_are_allowlisted(failures)
    check_public_api_snapshots(failures)
    check_python_connectivity_semantics(failures)
    check_entity_ref_not_kernel_traversal_handle(failures)
    check_no_raw_structural_throws(failures)
    check_subsystem_back_references(failures)
    check_subsystem_sources_have_real_logic(failures)
    check_no_public_submodel_mutators(failures)
    check_no_public_submodel_storage_accessors(failures)
    check_private_storage_headers_stay_private(failures)
    check_no_submodel_derivation_escape_hatches(failures)

    if failures:
        for failure in failures:
            print(f"architecture boundary violation: {failure}", file=sys.stderr)
        return 1
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run representative bad-sample tests for this checker",
    )
    args = parser.parse_args(argv)
    if args.self_test:
        return run_self_tests()
    return run_checks()


if __name__ == "__main__":
    raise SystemExit(main())
