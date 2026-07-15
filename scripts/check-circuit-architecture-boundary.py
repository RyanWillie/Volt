#!/usr/bin/env python3
"""Check Volt kernel architecture boundaries that should not regress."""

from __future__ import annotations

import argparse
import ast
import re
import sys
import textwrap
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CODE_DIRS = [ROOT / "include", ROOT / "src", ROOT / "tests", ROOT / "examples", ROOT / "python"]
ARCHITECTURE_CODE_DIRS = [ROOT / "include", ROOT / "src"]
NATIVE_CPP_DIRS = [
    ROOT / "include",
    ROOT / "src",
    ROOT / "tests",
    ROOT / "examples",
    ROOT / "benchmarks",
]
EVIDENCE_DIR = ROOT / "tests" / "architecture"

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
    "CircuitEntityTraits",
    "ConnectivityMutator",
    "HierarchyMutator",
    "ElectricalMutator",
    "IntentMutator",
    "NetClassMutator",
    "MutatorKey",
)
REMOVED_CIRCUIT_SUBSYSTEM_SYMBOLS = frozenset(
    {
        "ConnectivityModel",
        "HierarchyModel",
        "ElectricalModel",
        "DesignIntent",
        "NetClasses",
        "SubsystemStorage",
    }
)
FORBIDDEN_PUBLIC_CIRCUIT_HEADERS = frozenset(
    {
        "include/volt/circuit/connectivity/connectivity_model.hpp",
        "include/volt/circuit/detail/subsystem_storage.hpp",
        "include/volt/circuit/electrical/electrical_model.hpp",
        "include/volt/circuit/hierarchy/hierarchy_model.hpp",
    }
)
FORBIDDEN_PUBLIC_SCHEMATIC_HEADERS = frozenset(
    {
        "include/volt/schematic/schematic_items_model.hpp",
        "include/volt/schematic/schematic_library_model.hpp",
        "include/volt/schematic/schematic_sheet_model.hpp",
    }
)
REMOVED_SCHEMATIC_SUBMODEL_SYMBOLS = frozenset(
    {"SchematicItemsModel", "SchematicLibraryModel", "SchematicSheetModel"}
)
CIRCUIT_FACADE_TERMINOLOGY_PATTERN = re.compile(r"\bfacades?\b", flags=re.IGNORECASE)
CIRCUIT_PUBLIC_SHELL_NAME_PATTERN = re.compile(
    r"\b(?:class|struct)\s+(?:\[\[(?:(?!\]\])[\s\S])*\]\]\s*)*"
    r"([A-Za-z_]\w*(?:Model|Storage|Store|Container|Facade|Subsystem|Repository))\b"
)
CIRCUIT_PUBLIC_STORAGE_PATTERN = re.compile(
    r"\bEntityTable\s*<|\b(?:shared_ptr|unique_ptr)\s*<"
    r"|\b[A-Za-z_]\w*(?:State|Storage|Store)\b"
    r"|\b(?:std::)?(?:vector|deque|list|map|unordered_map)\s*<[^;{}]*\b(?:PinDefinition|ComponentDefinition|ComponentInstance|PinInstance|Net|ModuleDefinition|TemplateNetDefinition|PortDefinition|ModuleComponentTemplate|ModuleInstance|PortBinding|NetClass)\b"
)
CIRCUIT_PUBLIC_TYPE_DECLARATION_PATTERN = re.compile(
    r"\b(enum\s+class|class|struct)\s+"
    r"(?:\[\[(?:(?!\]\])[\s\S])*\]\]\s*)*([A-Za-z_]\w*)\b"
    r"|\busing\s+(?:\[\[(?:(?!\]\])[\s\S])*\]\]\s*)*([A-Za-z_]\w*)\s*"
    r"(?:\[\[(?:(?!\]\])[\s\S])*\]\]\s*)*="
)
CIRCUIT_PUBLIC_TYPE_SNAPSHOT = EVIDENCE_DIR / "circuit_public_types.txt"
CIRCUIT_OWNER_MUTATOR_PATTERN = re.compile(
    r"\b(?:add|append|assign|attach|bind|clear|connect|create|define|detach|disconnect|emplace|erase|insert|instantiate|link|mark|merge|push|record|remove|replace|restore|split|unlink|update)(?:_[A-Za-z_]\w*)?\s*\("
)
CIRCUIT_LOCAL_VALUE_MUTATORS = {
    (
        "include/volt/circuit/connectivity/nets.hpp",
        "bool connect(PinId pin);",
    ): "Net values retain local pin-list behavior; Circuit owns every canonical Net as const state.",
    (
        "include/volt/circuit/connectivity/nets.hpp",
        "bool disconnect(PinId pin);",
    ): "Net values retain local pin-list behavior; Circuit owns every canonical Net as const state.",
}
CIRCUIT_COMPLETE_INPUT_COLLECTIONS = {
    (
        "include/volt/circuit/hierarchy/hierarchy.hpp",
        "std::vector<TemplateNetDefinition> template_nets = {};",
    ): "ModuleSpec is one complete definition input, not canonical storage.",
    (
        "include/volt/circuit/hierarchy/hierarchy.hpp",
        "std::vector<ModuleComponentTemplate> components = {};",
    ): "ModuleSpec is one complete definition input, not canonical storage.",
}
RETAINED_CIRCUIT_DOMAIN_SNAPSHOTS = {
    "component_assembly_intent": (
        "ComponentAssemblyIntent",
        ROOT / "include" / "volt" / "circuit" / "intent" / "design_intent.hpp",
    ),
    "net_class": (
        "NetClass",
        ROOT / "include" / "volt" / "circuit" / "constraints" / "net_classes.hpp",
    ),
}
CIRCUIT_PUBLIC_DECLARATION_COUNT = 16
CIRCUIT_REQUIRED_PUBLIC_METHOD_DECLARATIONS = frozenset(
    """
[[nodiscard]] ComponentDefId define_component(ComponentSpec spec)
[[nodiscard]] ModuleDefId define_module(ModuleSpec spec)
[[nodiscard]] NetClassId define_net_class(NetClassSpec spec)
[[nodiscard]] ComponentId instantiate_component(ComponentDefId definition, ComponentInstanceSpec spec)
[[nodiscard]] ModuleInstanceId instantiate_module(ModuleDefId definition, ModuleInstanceSpec spec)
[[nodiscard]] NetId add_net(NetSpec spec)
bool connect(NetId net, PinId pin)
bool disconnect(PinId pin)
[[nodiscard]] PortBindingId bind_port(ModuleInstanceId instance, PortDefId port, NetId parent_net)
void update(ComponentId component, ComponentUpdate change)
void update(NetId net, NetUpdate change)
void mark_no_connect(PinId pin)
template <CircuitEntityId Id> [[nodiscard]] const entity_type_t<Id> &get(Id id) const
template <CircuitEntityId Id> [[nodiscard]] entity_range_t<Id> all() const &
template <CircuitEntityId Id> [[nodiscard]] entity_range_t<Id> all() const && = delete
[[nodiscard]] std::optional<NetId> net_of(PinId pin) const
""".strip().splitlines()
)

CIRCUIT_LEGACY_PUBLIC_DECLARATIONS = frozenset(
    """
[[nodiscard]] ModuleInstanceId instantiate_root_module(ModuleDefId definition, ModuleInstanceName name)
[[nodiscard]] ComponentId instantiate_component(ComponentDefId definition, ReferenceDesignator reference, PropertyMap properties = {})
[[nodiscard]] const std::optional<PhysicalPart> &selected_physical_part(ComponentId component) const
[[nodiscard]] const ElectricalAttributeMap &component_electrical_attributes(ComponentId component) const
[[nodiscard]] const ElectricalAttributeMap &pin_definition_electrical_attributes(PinDefId pin_definition) const
[[nodiscard]] const ElectricalAttributeMap &net_electrical_attributes(NetId net) const
[[nodiscard]] std::vector<ModulePinConnection> module_pin_connections(ModuleDefId module) const
[[nodiscard]] std::vector<std::pair<TemplateNetDefId, NetId>> module_net_origins(ModuleInstanceId instance) const
[[nodiscard]] std::vector<std::pair<ModuleComponentId, ComponentId>> module_component_origins(ModuleInstanceId instance) const
[[nodiscard]] bool is_intentional_stub_net(NetId net) const
[[nodiscard]] bool is_intentional_no_connect_pin(PinId pin) const
[[nodiscard]] std::optional<bool> component_dnp(ComponentId component) const
[[nodiscard]] bool is_component_selection_override(ComponentId component) const
[[nodiscard]] const std::vector<NetId> &intentional_stub_nets() const noexcept
[[nodiscard]] const std::vector<PinId> &intentional_no_connect_pins() const noexcept
[[nodiscard]] const std::vector<ComponentAssemblyIntent> &component_assembly_intents() const noexcept
[[nodiscard]] std::optional<NetClassId> net_class_by_name(const NetClassName &name) const
[[nodiscard]] std::optional<NetClassId> net_class_for_net(NetId net) const
[[nodiscard]] const std::vector<std::pair<NetId, NetClassId>> &net_class_assignments() const noexcept
""".strip().splitlines()
)

CIRCUIT_STORAGE_SHAPED_READ_NAMES = frozenset(
    {
        "selected_physical_part",
        "component_electrical_attributes",
        "pin_definition_electrical_attributes",
        "net_electrical_attributes",
        "module_pin_connections",
        "module_net_origins",
        "module_component_origins",
        "is_intentional_stub_net",
        "is_intentional_no_connect_pin",
        "component_dnp",
        "is_component_selection_override",
        "intentional_stub_nets",
        "intentional_no_connect_pins",
        "component_assembly_intents",
        "net_class_by_name",
        "net_class_for_net",
        "net_class_assignments",
    }
)

CIRCUIT_ENTITY_RANGE_PUBLIC_DECLARATIONS = frozenset(
    {
        "class iterator",
        "[[nodiscard]] iterator begin() const noexcept",
        "[[nodiscard]] iterator end() const noexcept",
        "[[nodiscard]] std::size_t size() const noexcept",
    }
)

CIRCUIT_ENTITY_RANGE_ITERATOR_PUBLIC_SURFACE = (
    "using value_type = entity_type_t<Id>; "
    "using difference_type = std::ptrdiff_t; "
    "using reference = const value_type &; "
    "using pointer = const value_type *; "
    "using iterator_concept = std::forward_iterator_tag; "
    "using iterator_category = std::forward_iterator_tag; "
    "iterator() = default; "
    "[[nodiscard]] reference operator*() const { return circuit_->get(Id{index_}); } "
    "[[nodiscard]] pointer operator->() const { return &**this; } "
    "iterator &operator++() { ++index_; return *this; } "
    "iterator operator++(int) { auto previous = *this; ++*this; return previous; } "
    "friend bool operator==(const iterator &, const iterator &) = default; "
    "iterator(const Circuit &circuit, std::size_t index) noexcept "
    ": circuit_{&circuit}, index_{index} {} "
    "iterator(const Circuit &&, std::size_t) = delete;"
)

SCHEMATIC_PUBLIC_DECLARATION_COUNT = 18
SCHEMATIC_REQUIRED_PUBLIC_METHOD_DECLARATIONS = frozenset(
    """
explicit Schematic(const Circuit &circuit)
explicit Schematic(const Circuit &&circuit) = delete
[[nodiscard]] SymbolDefId add_symbol_definition(SymbolDefinition definition)
[[nodiscard]] SheetId add_sheet(Sheet sheet)
[[nodiscard]] SheetRegionId add_sheet_region(SheetId sheet, SheetRegion region)
[[nodiscard]] SymbolInstanceId place_symbol(SheetId sheet, SymbolInstance instance)
[[nodiscard]] JunctionId add_junction(SheetId sheet, Junction junction)
[[nodiscard]] PowerPortId add_power_port(SheetId sheet, PowerPort port)
[[nodiscard]] NoConnectMarkerId add_no_connect_marker(SheetId sheet, NoConnectMarker marker)
[[nodiscard]] SheetPortId add_sheet_port(SheetId sheet, SheetPort port)
[[nodiscard]] SymbolFieldId add_symbol_field(SheetId sheet, SymbolField field)
[[nodiscard]] WireRunId add_wire_run(SheetId sheet, WireRun wire)
[[nodiscard]] NetLabelId add_net_label(SheetId sheet, NetLabel label)
void move(SchematicMove change)
template <SchematicEntityId Id> [[nodiscard]] const schematic_entity_type_t<Id> &get(Id id) const
template <SchematicEntityId Id> [[nodiscard]] schematic_entity_range_t<Id> all() const &
template <SchematicEntityId Id> [[nodiscard]] schematic_entity_range_t<Id> all() const && = delete
[[nodiscard]] const Circuit &circuit() const noexcept
""".strip().splitlines()
)

SCHEMATIC_LEGACY_PUBLIC_DECLARATIONS = frozenset(
    """
void replace_with(Schematic replacement)
[[nodiscard]] std::size_t add_sheet_region(SheetId sheet, SheetRegion region)
[[nodiscard]] PowerPortId add_terminal_marker(SheetId sheet, PowerPort marker)
[[nodiscard]] const SymbolDefinition &symbol_definition(SymbolDefId id) const
[[nodiscard]] const Sheet &sheet(SheetId id) const
[[nodiscard]] const SheetRegion &sheet_region(SheetId sheet, std::size_t region) const
[[nodiscard]] const SymbolInstance &symbol_instance(SymbolInstanceId id) const
[[nodiscard]] const WireRun &wire_run(WireRunId id) const
[[nodiscard]] const NetLabel &net_label(NetLabelId id) const
void move_net_label_text(NetLabelId id, Point position)
[[nodiscard]] const Junction &junction(JunctionId id) const
[[nodiscard]] const PowerPort &power_port(PowerPortId id) const
void move_power_port_label(PowerPortId id, Point position)
[[nodiscard]] const NoConnectMarker &no_connect_marker(NoConnectMarkerId id) const
[[nodiscard]] const SheetPort &sheet_port(SheetPortId id) const
[[nodiscard]] const SymbolField &symbol_field(SymbolFieldId id) const
void move_symbol_field(SymbolFieldId id, Point position)
[[nodiscard]] std::size_t symbol_definition_count() const noexcept
[[nodiscard]] std::size_t sheet_count() const noexcept
[[nodiscard]] std::size_t symbol_instance_count() const noexcept
[[nodiscard]] std::size_t wire_run_count() const noexcept
[[nodiscard]] std::size_t net_label_count() const noexcept
[[nodiscard]] std::size_t junction_count() const noexcept
[[nodiscard]] std::size_t power_port_count() const noexcept
[[nodiscard]] std::size_t no_connect_marker_count() const noexcept
[[nodiscard]] std::size_t sheet_port_count() const noexcept
[[nodiscard]] std::size_t symbol_field_count() const noexcept
""".strip().splitlines()
)

SCHEMATIC_STORAGE_SHAPED_READ_NAMES = frozenset(
    {
        "symbol_definition",
        "sheet",
        "sheet_region",
        "symbol_instance",
        "wire_run",
        "net_label",
        "junction",
        "power_port",
        "no_connect_marker",
        "sheet_port",
        "symbol_field",
        "symbol_definition_count",
        "sheet_count",
        "symbol_instance_count",
        "wire_run_count",
        "net_label_count",
        "junction_count",
        "power_port_count",
        "no_connect_marker_count",
        "sheet_port_count",
        "symbol_field_count",
    }
)

SCHEMATIC_ENTITY_RANGE_PUBLIC_DECLARATIONS = frozenset(
    {
        "class iterator",
        "[[nodiscard]] iterator begin() const noexcept",
        "[[nodiscard]] iterator end() const noexcept",
        "[[nodiscard]] std::size_t size() const noexcept",
        "SchematicEntityRange(const Schematic &schematic, std::size_t size) noexcept",
    }
)

SCHEMATIC_ENTITY_RANGE_ITERATOR_PUBLIC_SURFACE = (
    "using value_type = schematic_entity_type_t<Id>; "
    "using difference_type = std::ptrdiff_t; "
    "using reference = const value_type &; "
    "using pointer = const value_type *; "
    "using iterator_concept = std::forward_iterator_tag; "
    "using iterator_category = std::forward_iterator_tag; "
    "iterator() = default; "
    "[[nodiscard]] reference operator*() const { return schematic_->get(Id{index_}); } "
    "[[nodiscard]] pointer operator->() const { return &**this; } "
    "iterator &operator++() { ++index_; return *this; } "
    "iterator operator++(int) { auto previous = *this; ++*this; return previous; } "
    "[[nodiscard]] bool operator==(const iterator &other) const noexcept "
    "{ return schematic_ == other.schematic_ && index_ == other.index_; } "
    "iterator(const Schematic &schematic, std::size_t index) noexcept "
    ": schematic_{&schematic}, index_{index} {} "
    "iterator(const Schematic &&, std::size_t) = delete;"
)

ENTITY_REF_REPORTING_CLASSIFICATIONS = {
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
    "ComponentInstance": (
        ROOT / "include" / "volt" / "circuit" / "connectivity" / "instances.hpp",
        {"set_property"},
    ),
    "PhysicalPart": (
        ROOT / "include" / "volt" / "circuit" / "parts" / "parts.hpp",
        {"set_electrical_attribute"},
    ),
    "ModuleDefinition": (
        ROOT / "include" / "volt" / "circuit" / "hierarchy" / "hierarchy.hpp",
        {"add_template_net", "add_port", "add_component"},
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
}

PRIVATE_STORAGE_ADAPTER_DERIVATIONS = {
    (
        "src/schematic/schematic_storage.hpp",
        "SheetStorage",
        "Sheet",
    ): "Schematic source-private storage adapter exposes sheet membership mutation only after Schematic preflight.",
}

PRIVATE_STORAGE_HEADER_OWNERS = {
    "src/pcb/routing/board_spatial_index_storage.hpp": ("src/pcb/routing/",),
    "src/schematic/schematic_storage.hpp": ("src/schematic/",),
}


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


def circuit_architecture_files() -> list[Path]:
    files: list[Path] = []
    for directory in (ROOT / "include" / "volt" / "circuit", ROOT / "src" / "circuit"):
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if path.suffix in {".cpp", ".hpp", ".h"}:
                files.append(path)
    return sorted(files)


def native_cpp_files() -> list[Path]:
    files: list[Path] = []
    for directory in NATIVE_CPP_DIRS:
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if path.suffix not in {".cpp", ".hpp", ".h"}:
                continue
            if is_python_binding_source(path):
                continue
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


def declaration_chunks(section: str) -> list[str]:
    section = re.sub(r"(\)\s*(?:const\s*)?(?:noexcept\s*)?)\{[^{}]*\}", r"\1;", section)
    return [normalize_declaration(chunk) for chunk in section.split(";")]


def public_declarations(class_name: str, header_path: Path) -> list[str]:
    return public_declarations_from_header(class_name, read(header_path))


def public_declarations_from_header(class_name: str, header: str) -> list[str]:
    body = class_body(strip_comments(header), class_name)
    return declarations_from_accesses(body, {"public"})


def public_surface_declarations_from_header(
    class_name: str, header: str, access_names: set[str] | None = None
) -> list[str]:
    body = class_body(strip_comments(header), class_name)
    selected_accesses = {"public"} if access_names is None else access_names
    declarations: list[str] = []
    nested_type_pattern = re.compile(r"\b(class|struct)\s+([A-Za-z_]\w*)\b[^;{]*\{")
    for access, section in access_sections(body):
        if access not in selected_accesses:
            continue
        for match in nested_type_pattern.finditer(section):
            declarations.append(f"{match.group(1)} {match.group(2)}")
        for chunk in declaration_chunks(strip_nested_type_definitions(section)):
            if chunk:
                declarations.append(chunk)
    return declarations


def normalized_access_surface_from_header(
    class_name: str, header: str, access_names: set[str]
) -> str:
    body = class_body(strip_comments(header), class_name)
    selected = " ".join(
        section for access, section in access_sections(body) if access in access_names
    )
    return re.sub(r"\s+", " ", selected).strip()


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
        # count as deriving from a mutating submodel.
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


def exact_identifier_lines(text: str, identifiers: frozenset[str]) -> list[tuple[int, str, str]]:
    matches: list[tuple[int, str, str]] = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        for identifier in sorted(identifiers):
            if re.search(rf"\b{re.escape(identifier)}\b", line):
                matches.append((line_number, identifier, line.strip()))
    return matches


def circuit_facade_terminology_lines(text: str) -> list[tuple[int, str]]:
    return [
        (line_number, line.strip())
        for line_number, line in enumerate(text.splitlines(), start=1)
        if CIRCUIT_FACADE_TERMINOLOGY_PATTERN.search(line)
    ]


def circuit_public_shell_names(text: str) -> list[str]:
    stripped = strip_cpp_comments_and_strings_preserve_lines(text)
    return CIRCUIT_PUBLIC_SHELL_NAME_PATTERN.findall(stripped)


def circuit_public_storage_lines(text: str) -> list[tuple[int, str]]:
    stripped = strip_cpp_comments_and_strings_preserve_lines(text)
    original_lines = text.splitlines()
    matches: list[tuple[int, str]] = []
    for match in CIRCUIT_PUBLIC_STORAGE_PATTERN.finditer(stripped):
        line_number = stripped.count("\n", 0, match.start()) + 1
        matches.append((line_number, original_lines[line_number - 1].strip()))
    return matches


def circuit_owner_mutator_lines(text: str) -> list[tuple[int, str]]:
    stripped = strip_cpp_comments_and_strings_preserve_lines(text)
    original_lines = text.splitlines()
    matches: list[tuple[int, str]] = []
    for match in CIRCUIT_OWNER_MUTATOR_PATTERN.finditer(stripped):
        line_number = stripped.count("\n", 0, match.start()) + 1
        matches.append((line_number, original_lines[line_number - 1].strip()))
    return matches


def is_public_circuit_header(path: Path) -> bool:
    public_circuit_root = ROOT / "include" / "volt" / "circuit"
    return path.suffix in {".h", ".hpp"} and path.is_relative_to(public_circuit_root)


def public_circuit_headers() -> list[Path]:
    public_circuit_root = ROOT / "include" / "volt" / "circuit"
    return sorted(path for path in public_circuit_root.rglob("*") if is_public_circuit_header(path))


def circuit_public_type_declarations(path: Path, text: str) -> list[str]:
    stripped = strip_cpp_comments_and_strings_preserve_lines(text)
    declarations: list[str] = []
    for match in CIRCUIT_PUBLIC_TYPE_DECLARATION_PATTERN.finditer(stripped):
        if match.group(3) is not None:
            kind = "using"
            name = match.group(3)
        else:
            kind = re.sub(r"\s+", " ", match.group(1))
            name = match.group(2)
        declarations.append(f"{relative(path)}|{kind}|{name}")
    return declarations


def circuit_public_type_inventory_failures(
    actual: list[str], expected: list[str]
) -> list[str]:
    failures: list[str] = []
    actual_counts = Counter(actual)
    expected_counts = Counter(expected)
    for declaration in sorted((actual_counts - expected_counts).elements()):
        failures.append(
            "unexpected public Circuit-header type declaration; canonical logical ownership "
            f"must remain with Circuit: {declaration}"
        )
    for declaration in sorted((expected_counts - actual_counts).elements()):
        failures.append(f"required public Circuit-header type declaration is missing: {declaration}")
    return failures


def exact_required_occurrence_failures(
    found: Counter[tuple[str, str]], required: dict[tuple[str, str], str], label: str
) -> list[str]:
    failures: list[str] = []
    for key, reason in sorted(required.items()):
        if not reason.strip():
            failures.append(f"required {label} entry {key!r} needs a semantic explanation")
        count = found[key]
        if count != 1:
            failures.append(
                f"required {label} entry {key!r} matched {count} times; expected exactly once"
            )
    return failures


def is_forbidden_public_circuit_header(path: str | Path) -> bool:
    normalized = Path(path).as_posix()
    return normalized in FORBIDDEN_PUBLIC_CIRCUIT_HEADERS


def is_forbidden_public_schematic_header(path: str | Path) -> bool:
    normalized = Path(path).as_posix()
    return normalized in FORBIDDEN_PUBLIC_SCHEMATIC_HEADERS


def check_rejected_tokens(failures: list[str]) -> None:
    for path in code_files():
        text = read(path)
        for token in REJECTED_TOKENS:
            if token in text:
                fail(f"{relative(path)} contains rejected architecture token {token}", failures)


def check_no_removed_circuit_architecture(failures: list[str]) -> None:
    for path in code_files():
        first_match_by_symbol: dict[str, tuple[int, str]] = {}
        for line_number, symbol, line in exact_identifier_lines(read(path), REMOVED_CIRCUIT_SUBSYSTEM_SYMBOLS):
            first_match_by_symbol.setdefault(symbol, (line_number, line))
        for symbol, (line_number, line) in sorted(first_match_by_symbol.items()):
            fail(
                f"{relative(path)}:{line_number} references removed public Circuit subsystem "
                f"symbol {symbol}: {line}",
                failures,
            )

    for header in sorted(FORBIDDEN_PUBLIC_CIRCUIT_HEADERS):
        if (ROOT / header).exists():
            fail(
                f"{header} is forbidden public Circuit storage plumbing; storage must remain "
                "inside the Circuit implementation boundary",
                failures,
            )

    found_complete_input_collections: Counter[tuple[str, str]] = Counter()
    for path in public_circuit_headers():
        names = circuit_public_shell_names(read(path))
        if names:
            fail(
                f"{relative(path)} declares replacement public Circuit shell {names[0]}; "
                "canonical storage belongs only to Circuit",
                failures,
            )
        if path == ROOT_TYPES["Circuit"]:
            continue
        for line_number, line in circuit_public_storage_lines(read(path)):
            key = (relative(path), line)
            if key in CIRCUIT_COMPLETE_INPUT_COLLECTIONS:
                found_complete_input_collections[key] += 1
                continue
            fail(
                f"{relative(path)}:{line_number} exposes public Circuit storage plumbing: "
                f"{line}",
                failures,
            )

    failures.extend(
        exact_required_occurrence_failures(
            found_complete_input_collections,
            CIRCUIT_COMPLETE_INPUT_COLLECTIONS,
            "complete Circuit input collection",
        )
    )

    for path in circuit_architecture_files():
        matches = circuit_facade_terminology_lines(read(path))
        if matches:
            line_number, line = matches[0]
            fail(
                f"{relative(path)}:{line_number} retains obsolete Circuit facade terminology: "
                f"{line}",
                failures,
            )


def check_no_removed_schematic_architecture(failures: list[str]) -> None:
    for path in code_files():
        first_match_by_symbol: dict[str, tuple[int, str]] = {}
        for line_number, symbol, line in exact_identifier_lines(
            read(path), REMOVED_SCHEMATIC_SUBMODEL_SYMBOLS
        ):
            first_match_by_symbol.setdefault(symbol, (line_number, line))
        for symbol, (line_number, line) in sorted(first_match_by_symbol.items()):
            fail(
                f"{relative(path)}:{line_number} references removed public Schematic storage "
                f"symbol {symbol}: {line}",
                failures,
            )

    for header in sorted(FORBIDDEN_PUBLIC_SCHEMATIC_HEADERS):
        if (ROOT / header).exists():
            fail(
                f"{header} is forbidden public Schematic storage plumbing; storage must remain "
                "inside the Schematic implementation boundary",
                failures,
            )


def check_circuit_public_type_inventory(failures: list[str]) -> None:
    if not CIRCUIT_PUBLIC_TYPE_SNAPSHOT.exists():
        fail(f"{relative(CIRCUIT_PUBLIC_TYPE_SNAPSHOT)} is missing", failures)
        return

    actual = [
        declaration
        for path in public_circuit_headers()
        for declaration in circuit_public_type_declarations(path, read(path))
    ]
    expected = [
        line.rstrip()
        for line in read(CIRCUIT_PUBLIC_TYPE_SNAPSHOT).splitlines()
        if line.strip() and not line.startswith("#")
    ]
    failures.extend(circuit_public_type_inventory_failures(actual, expected))

    for path in public_circuit_headers():
        stripped = strip_cpp_comments_and_strings_preserve_lines(read(path))
        match = re.search(r"\btypedef\b", stripped)
        if match is not None:
            line_number = stripped.count("\n", 0, match.start()) + 1
            fail(
                f"{relative(path)}:{line_number} uses an unexpected typedef in the public "
                "Circuit header boundary",
                failures,
            )


def check_no_non_root_circuit_owners(failures: list[str]) -> None:
    found_local_value_mutators: Counter[tuple[str, str]] = Counter()
    for path in public_circuit_headers():
        if path == ROOT_TYPES["Circuit"]:
            continue
        for line_number, line in circuit_owner_mutator_lines(read(path)):
            key = (relative(path), line)
            if key in CIRCUIT_LOCAL_VALUE_MUTATORS:
                found_local_value_mutators[key] += 1
                continue
            fail(
                f"{relative(path)}:{line_number} exposes an unexpected root-like mutator "
                f"outside Circuit: {line}",
                failures,
            )

    failures.extend(
        exact_required_occurrence_failures(
            found_local_value_mutators,
            CIRCUIT_LOCAL_VALUE_MUTATORS,
            "local-value mutator",
        )
    )


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


def check_no_flat_pcb_public_headers(failures: list[str]) -> None:
    pcb_dir = ROOT / "include" / "volt" / "pcb"
    for path in sorted(pcb_dir.glob("*.hpp")):
        if path not in ALLOWED_FLAT_PCB_HEADERS:
            fail(
                f"{path.relative_to(ROOT)} is a flat PCB public header; use an ownership-folder path instead",
                failures,
            )


def check_public_api_evidence(failures: list[str]) -> None:
    snapshots = {
        class_name.lower(): (class_name, header_path)
        for class_name, header_path in ROOT_TYPES.items()
    }
    snapshots.update(RETAINED_CIRCUIT_DOMAIN_SNAPSHOTS)
    for snapshot_name, (class_name, header_path) in snapshots.items():
        evidence_path = EVIDENCE_DIR / f"{snapshot_name}_public_api.txt"
        if not evidence_path.exists():
            fail(f"{relative(evidence_path)} is missing", failures)
            continue

        actual = public_api_snapshot(class_name, header_path)
        expected = [
            line.rstrip()
            for line in read(evidence_path).splitlines()
            if not line.startswith("#")
        ]
        if actual != expected:
            fail(
                f"{relative(evidence_path)} review evidence does not match "
                f"{relative(header_path)}; inspect the API delta before updating the snapshot",
                failures,
            )


def check_python_connectivity_semantics(failures: list[str]) -> None:
    for path in python_pcb_authoring_files():
        for risk in python_connectivity_risks(path, read(path)):
            fail(
                f"{relative(path)}:{risk.line} function {risk.qualname} owns PCB "
                f"connectivity/net inference in Python ({risk.reason}): {risk.line_text}",
                failures,
            )


def check_entity_ref_not_kernel_traversal_handle(failures: list[str]) -> None:
    found_reporting_classifications: set[tuple[str, str]] = set()
    for path in entity_ref_sensitive_files():
        for line_number, line in entity_ref_traversal_lines(read(path)):
            key = (relative(path), line)
            if key in ENTITY_REF_REPORTING_CLASSIFICATIONS:
                found_reporting_classifications.add(key)
                continue
            fail(
                f"{relative(path)}:{line_number} branches on or unwraps EntityRef in a "
                f"kernel-owned layer: {line}",
                failures,
            )

    for key, reason in sorted(ENTITY_REF_REPORTING_CLASSIFICATIONS.items()):
        if not reason.strip():
            fail(
                f"EntityRef reporting classification {key!r} must document why it is reporting-only",
                failures,
            )
        if key not in found_reporting_classifications:
            fail(f"EntityRef reporting classification {key!r} no longer matches source", failures)


def check_no_raw_structural_throws(failures: list[str]) -> None:
    for path in source_code_files():
        for line_number, line in raw_structural_throw_lines(read(path)):
            fail(
                f"{relative(path)}:{line_number} throws raw structural std exception; "
                f"use a typed volt::Kernel*Error with an ErrorCode: {line}",
                failures,
            )


def circuit_contract_configuration_failures(
    required: frozenset[str] = CIRCUIT_REQUIRED_PUBLIC_METHOD_DECLARATIONS,
    legacy: frozenset[str] = CIRCUIT_LEGACY_PUBLIC_DECLARATIONS,
    expected_count: int = CIRCUIT_PUBLIC_DECLARATION_COUNT,
) -> list[str]:
    failures: list[str] = []
    if len(required) != expected_count:
        failures.append(
            f"Circuit required declaration set has {len(required)} entries; expected "
            f"{expected_count}"
        )

    overlap = sorted(required & legacy)
    for declaration in overlap:
        failures.append(
            "Circuit declaration is both required and legacy, so the retired surface could "
            f"regrow: {declaration}"
        )

    for declaration in sorted(required):
        name = declaration_function_name(declaration)
        if name in CIRCUIT_STORAGE_SHAPED_READ_NAMES:
            failures.append(
                "Circuit required declaration set contains storage-shaped derived read "
                f"{name}: {declaration}"
            )
    return failures


def circuit_public_method_admission_failures(header_text: str) -> list[str]:
    failures: list[str] = []

    stripped_header = strip_cpp_comments_and_strings_preserve_lines(header_text)
    class_match = re.search(r"\bclass\s+Circuit\b([^;{]*)\{", stripped_header)
    if class_match is None:
        failures.append("Circuit must be declared exactly as `class Circuit final`")
        return failures

    class_suffix = re.sub(r"\s+", " ", class_match.group(1)).strip()
    if class_suffix != "final":
        failures.append(
            "Circuit must be declared exactly as `class Circuit final` with no base classes"
        )

    public_declarations = public_surface_declarations_from_header(
        "Circuit", header_text, {"public"}
    )
    protected_declarations = public_surface_declarations_from_header(
        "Circuit", header_text, {"protected"}
    )

    declaration_counts = Counter(public_declarations)
    for declaration in sorted(CIRCUIT_REQUIRED_PUBLIC_METHOD_DECLARATIONS):
        count = declaration_counts[declaration]
        if count == 0:
            failures.append(
                "Circuit public contract is missing required declaration: " f"{declaration}"
            )
        elif count > 1:
            failures.append(
                "Circuit public contract duplicates required declaration: " f"{declaration}"
            )

    if len(public_declarations) != CIRCUIT_PUBLIC_DECLARATION_COUNT:
        failures.append(
            f"Circuit public contract has {len(public_declarations)} declarations; expected "
            f"exactly {CIRCUIT_PUBLIC_DECLARATION_COUNT}"
        )

    for declaration in public_declarations:
        name = declaration_function_name(declaration)
        if name in CIRCUIT_STORAGE_SHAPED_READ_NAMES:
            failures.append(
                "Circuit public storage-shaped derived read belongs in a typed free "
                f"query: {declaration}"
            )
            continue
        if declaration in CIRCUIT_LEGACY_PUBLIC_DECLARATIONS:
            failures.append(
                "Circuit public declaration is a retired pre-1.0 construction or read "
                f"surface: {declaration}"
            )
            continue
        if declaration not in CIRCUIT_REQUIRED_PUBLIC_METHOD_DECLARATIONS:
            failures.append(
                "Circuit public declaration is outside the required typed aggregate "
                "contract: "
                f"{declaration}"
            )

    for declaration in protected_declarations:
        failures.append(
            "Circuit protected declaration is outside the public typed aggregate contract: "
            f"{declaration}"
        )
    return failures


def check_circuit_public_method_admission(failures: list[str]) -> None:
    header = ROOT_TYPES["Circuit"]
    failures.extend(circuit_public_method_admission_failures(read(header)))


def schematic_contract_configuration_failures(
    required: frozenset[str] = SCHEMATIC_REQUIRED_PUBLIC_METHOD_DECLARATIONS,
    legacy: frozenset[str] = SCHEMATIC_LEGACY_PUBLIC_DECLARATIONS,
    expected_count: int = SCHEMATIC_PUBLIC_DECLARATION_COUNT,
) -> list[str]:
    failures: list[str] = []
    if len(required) != expected_count:
        failures.append(
            f"Schematic required declaration set has {len(required)} entries; expected "
            f"{expected_count}"
        )

    overlap = sorted(required & legacy)
    for declaration in overlap:
        failures.append(
            "Schematic declaration is both required and legacy, so the retired surface could "
            f"regrow: {declaration}"
        )

    for declaration in sorted(required):
        name = declaration_function_name(declaration)
        if name in SCHEMATIC_STORAGE_SHAPED_READ_NAMES:
            failures.append(
                "Schematic required declaration set contains storage-shaped read "
                f"{name}: {declaration}"
            )
    return failures


def schematic_public_method_admission_failures(header_text: str) -> list[str]:
    failures: list[str] = []

    stripped_header = strip_cpp_comments_and_strings_preserve_lines(header_text)
    class_match = re.search(r"\bclass\s+Schematic\b([^;{]*)\{", stripped_header)
    if class_match is None:
        failures.append("Schematic must be declared exactly as `class Schematic final`")
        return failures

    class_suffix = re.sub(r"\s+", " ", class_match.group(1)).strip()
    if class_suffix != "final":
        failures.append(
            "Schematic must be declared exactly as `class Schematic final` with no base classes"
        )

    public_declarations = public_surface_declarations_from_header(
        "Schematic", header_text, {"public"}
    )
    protected_declarations = public_surface_declarations_from_header(
        "Schematic", header_text, {"protected"}
    )

    declaration_counts = Counter(public_declarations)
    for declaration in sorted(SCHEMATIC_REQUIRED_PUBLIC_METHOD_DECLARATIONS):
        count = declaration_counts[declaration]
        if count == 0:
            failures.append(
                "Schematic public contract is missing required declaration: " f"{declaration}"
            )
        elif count > 1:
            failures.append(
                "Schematic public contract duplicates required declaration: " f"{declaration}"
            )

    if len(public_declarations) != SCHEMATIC_PUBLIC_DECLARATION_COUNT:
        failures.append(
            f"Schematic public contract has {len(public_declarations)} declarations; expected "
            f"exactly {SCHEMATIC_PUBLIC_DECLARATION_COUNT}"
        )

    for declaration in public_declarations:
        name = declaration_function_name(declaration)
        if name in SCHEMATIC_STORAGE_SHAPED_READ_NAMES:
            failures.append(
                "Schematic public storage-shaped read belongs in generic typed get/all or a "
                f"free query: {declaration}"
            )
            continue
        if declaration in SCHEMATIC_LEGACY_PUBLIC_DECLARATIONS:
            failures.append(
                "Schematic public declaration is a retired storage-shaped surface: "
                f"{declaration}"
            )
            continue
        if declaration not in SCHEMATIC_REQUIRED_PUBLIC_METHOD_DECLARATIONS:
            failures.append(
                "Schematic public declaration is outside the required bounded owner-shaped "
                f"contract: {declaration}"
            )

    for declaration in protected_declarations:
        failures.append(
            "Schematic protected declaration is outside the public bounded owner-shaped "
            f"contract: {declaration}"
        )
    return failures


def check_schematic_public_method_admission(failures: list[str]) -> None:
    failures.extend(schematic_public_method_admission_failures(read(ROOT_TYPES["Schematic"])))


def circuit_range_construction_failures(header_text: str) -> list[str]:
    failures: list[str] = []
    for declaration in public_surface_declarations_from_header(
        "CircuitEntityRange", header_text, {"public", "protected"}
    ):
        declaration = declaration.lstrip("} ")
        if declaration not in CIRCUIT_ENTITY_RANGE_PUBLIC_DECLARATIONS:
            failures.append(
                "CircuitEntityRange public declaration is outside the borrowed-range contract: "
                f"{declaration}"
            )
    iterator_surface = normalized_access_surface_from_header(
        "iterator", header_text, {"public", "protected"}
    )
    if iterator_surface != CIRCUIT_ENTITY_RANGE_ITERATOR_PUBLIC_SURFACE:
        failures.append(
            "CircuitEntityRange::iterator public/protected surface is outside the required "
            f"forward-iterator contract: {iterator_surface}"
        )
    return failures


def check_circuit_range_construction(failures: list[str]) -> None:
    failures.extend(circuit_range_construction_failures(read(ROOT_TYPES["Circuit"])))


def schematic_range_construction_failures(header_text: str) -> list[str]:
    failures: list[str] = []
    for declaration in public_surface_declarations_from_header(
        "SchematicEntityRange", header_text, {"public", "protected"}
    ):
        declaration = declaration.lstrip("} ")
        if declaration not in SCHEMATIC_ENTITY_RANGE_PUBLIC_DECLARATIONS:
            failures.append(
                "SchematicEntityRange public declaration is outside the borrowed-range "
                f"contract: {declaration}"
            )
    iterator_surface = normalized_access_surface_from_header(
        "iterator", header_text, {"public", "protected"}
    )
    if iterator_surface != SCHEMATIC_ENTITY_RANGE_ITERATOR_PUBLIC_SURFACE:
        failures.append(
            "SchematicEntityRange::iterator public/protected surface is outside the required "
            f"forward-iterator contract: {iterator_surface}"
        )
    return failures


def check_schematic_range_construction(failures: list[str]) -> None:
    failures.extend(schematic_range_construction_failures(read(ROOT_TYPES["Schematic"])))


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
        for path, allowed_prefixes in PRIVATE_STORAGE_HEADER_OWNERS.items()
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

    for header_path in sorted(PRIVATE_STORAGE_HEADER_OWNERS):
        if not (ROOT / header_path).exists():
            fail(f"private storage header {header_path} is missing", failures)
        elif header_path not in found_headers:
            fail(f"private storage header {header_path} is not included by its owner", failures)


def check_no_submodel_derivation_escape_hatches(failures: list[str]) -> None:
    found_private_storage_adapters: set[tuple[str, str, str]] = set()
    for path in code_files():
        if path.suffix not in {".cpp", ".hpp", ".h"}:
            continue
        for derivation in submodel_derivations(path, read(path)):
            key = (relative(derivation.path), derivation.derived, derivation.base)
            if key in PRIVATE_STORAGE_ADAPTER_DERIVATIONS:
                found_private_storage_adapters.add(key)
                continue
            fail(
                f"{relative(derivation.path)}:{derivation.line} derives "
                f"{derivation.derived} from mutating submodel {derivation.base}; this can "
                "re-export protected storage mutators outside the owning aggregate root",
                failures,
            )

    for key, reason in sorted(PRIVATE_STORAGE_ADAPTER_DERIVATIONS.items()):
        if not reason.strip():
            fail(
                f"private storage adapter derivation {key!r} must document its owner boundary",
                failures,
            )
        if key not in found_private_storage_adapters:
            fail(f"private storage adapter derivation {key!r} no longer matches source", failures)


def require_self_test(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_self_tests() -> int:
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
        "public API parser must ignore nested type methods in the root surface",
    )
    require_self_test(
        public_declarations_from_header("Mutator", nested_public_sample) == ["void mutate()"],
        "nested type methods must remain observable for dedicated snapshots",
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
    removed_shell_sample = textwrap.dedent(
        """
        class ConnectivityModel {};
        class HierarchyModel {};
        class ElectricalModel {};
        class DesignIntent {};
        class NetClasses {};
        template <typename View, typename State> class SubsystemStorage {};
        """
    )
    removed_shell_matches = exact_identifier_lines(
        removed_shell_sample, REMOVED_CIRCUIT_SUBSYSTEM_SYMBOLS
    )
    require_self_test(
        {symbol for _, symbol, _ in removed_shell_matches}
        == REMOVED_CIRCUIT_SUBSYSTEM_SYMBOLS,
        "every retired public Circuit subsystem/storage shell must be reported",
    )
    allowed_domain_type_sample = textwrap.dedent(
        """
        class NetClass {};
        class ComponentAssemblyIntent {};
        """
    )
    require_self_test(
        not exact_identifier_lines(
            allowed_domain_type_sample, REMOVED_CIRCUIT_SUBSYSTEM_SYMBOLS
        ),
        "public domain value/entity types must not be mistaken for retired subsystem shells",
    )
    facade_terminology_sample = textwrap.dedent(
        """
        class Facade {};
        // read-only facade
        // borrowed facades
        """
    )
    require_self_test(
        len(circuit_facade_terminology_lines(facade_terminology_sample)) == 3,
        "Circuit facade terminology must be observable in identifiers and comments",
    )
    require_self_test(
        not circuit_facade_terminology_lines("void facade_connector();"),
        "unrelated identifiers containing the letters facade must not be reported",
    )
    replacement_shell_sample = textwrap.dedent(
        """
        class LogicalModel {};
        struct NetStorage {};
        class CircuitRepository {};
        class [[nodiscard]] AttributedStorage {};
        class PartModel3D {};
        """
    )
    require_self_test(
        circuit_public_shell_names(replacement_shell_sample)
        == ["LogicalModel", "NetStorage", "CircuitRepository", "AttributedStorage"],
        "renamed public model, storage, and repository shells must be reported",
    )
    public_storage_samples = [
        "EntityTable\n<Net, NetId> nets;",
        "std::shared_ptr<\ndetail::LogicalState> state;",
        "std::unique_ptr<\ndetail::LogicalImpl> implementation;",
        "std::vector<Net> nets;",
        "detail::LogicalState state;",
    ]
    require_self_test(
        all(circuit_public_storage_lines(sample) for sample in public_storage_samples),
        "canonical tables, state, entity collections, and pointer-owned implementations must "
        "each be reported",
    )
    public_type_sample_path = ROOT / "include" / "volt" / "circuit" / "sample.h"
    required_public_type_sample = "class Net {};"
    replacement_public_type_sample = textwrap.dedent(
        """
        class Net {};
        class [[nodiscard]] LogicalGraph {
            std::vector<Net> nets;
            detail::LogicalState state;
        };
        using LogicalStorage [[deprecated]] = std::vector<Net>;
        """
    )
    required_public_types = circuit_public_type_declarations(
        public_type_sample_path, required_public_type_sample
    )
    replacement_public_types = circuit_public_type_declarations(
        public_type_sample_path, replacement_public_type_sample
    )
    public_type_failures = circuit_public_type_inventory_failures(
        replacement_public_types, required_public_types
    )
    require_self_test(
        len(public_type_failures) == 2
        and any("LogicalGraph" in failure for failure in public_type_failures)
        and any("LogicalStorage" in failure for failure in public_type_failures),
        "renamed public owners and storage aliases must fail the exact type inventory",
    )
    require_self_test(
        circuit_owner_mutator_lines("NetId append_net(Net net);")
        and circuit_owner_mutator_lines(
            "void record_module_net_origin(TemplateNetDefId origin, NetId concrete);"
        )
        and not circuit_owner_mutator_lines("ComponentId component() const;"),
        "root-like and retired restoration mutators on public domain types must be reported",
    )
    duplicate_required_key = ("include/volt/circuit/sample.hpp", "bool connect(PinId pin);")
    duplicate_required_failures = exact_required_occurrence_failures(
        Counter({duplicate_required_key: 2}),
        {duplicate_required_key: "One documented local-value mutation."},
        "sample",
    )
    require_self_test(
        len(duplicate_required_failures) == 1
        and "matched 2 times" in duplicate_required_failures[0]
        and not exact_required_occurrence_failures(
            Counter({duplicate_required_key: 1}),
            {duplicate_required_key: "One documented local-value mutation."},
            "sample",
        ),
        "required storage and mutator declarations must match exactly once",
    )
    require_self_test(
        is_public_circuit_header(public_type_sample_path)
        and is_public_circuit_header(public_type_sample_path.with_suffix(".hpp"))
        and not is_public_circuit_header(public_type_sample_path.with_suffix(".cpp")),
        "public Circuit type and storage enforcement must cover both .h and .hpp headers",
    )
    require_self_test(
        all(is_forbidden_public_circuit_header(path) for path in FORBIDDEN_PUBLIC_CIRCUIT_HEADERS)
        and not is_forbidden_public_circuit_header(
            "include/volt/circuit/constraints/net_classes.hpp"
        ),
        "all removed public Circuit model/storage headers must stay forbidden",
    )
    require_self_test(
        all(
            is_forbidden_public_schematic_header(path)
            for path in FORBIDDEN_PUBLIC_SCHEMATIC_HEADERS
        )
        and not is_forbidden_public_schematic_header(
            "include/volt/schematic/schematic.hpp"
        ),
        "all removed public Schematic model/storage headers must stay forbidden",
    )
    removed_schematic_symbols = exact_identifier_lines(
        "class SchematicItemsModel {};\nclass SimilarSchematicItemsModel {};",
        REMOVED_SCHEMATIC_SUBMODEL_SYMBOLS,
    )
    require_self_test(
        len(removed_schematic_symbols) == 1
        and removed_schematic_symbols[0][1] == "SchematicItemsModel",
        "removed Schematic storage symbols must stay forbidden without substring false positives",
    )
    submodel_derivation_sample = textwrap.dedent(
        """
        struct TestSheet : volt::Sheet {
          public:
            using Sheet::add_region;
        };
        """
    )
    sample_derivations = submodel_derivations(Path("tests/sample.cpp"), submodel_derivation_sample)
    require_self_test(
        any(
            derivation.derived == "TestSheet"
            and derivation.base == "Sheet"
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
    additional_python_risk_sample = textwrap.dedent(
        """
        def _route_net(context, net, start, middle, end):
            start_net = context.pad_net(start)
            middle_net = context.pad_net(middle)
            return start_net if middle_net is None else middle_net
        """
    )
    additional_python_risks = python_connectivity_risks(
        Path("python/volt/_pcb_composition.py"), additional_python_risk_sample
    )
    require_self_test(
        any(
            "middle_net = context.pad_net(middle)" == risk.line_text
            for risk in additional_python_risks
        ),
        "every additional Python route-net inference must fail",
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

    circuit_admission_sample = textwrap.dedent(
        """
        class Circuit final {
          public:
            template <CircuitEntityId Id> [[nodiscard]] const entity_type_t<Id> &get(Id id) const;
            ComponentId add_component(ComponentInstance component);
            const Net &get(NetId id) const;
            NetId add_net(Net net);
            void update(EntityRef entity, PropertyValue value);
            ConnectivityModel connectivity;
            using RawMutationHandle = ConnectivityModel;
            struct Mutator {
                void mutate(Circuit &circuit);
            };
          protected:
            ConnectivityModel &connectivity();
        };
        """
    )
    admission_failures = circuit_public_method_admission_failures(circuit_admission_sample)
    require_self_test(
        any("add_component" in failure for failure in admission_failures)
        and any("const Net &get" in failure for failure in admission_failures)
        and any("NetId add_net(Net net)" in failure for failure in admission_failures)
        and any("void update(EntityRef" in failure for failure in admission_failures)
        and any("ConnectivityModel connectivity" in failure for failure in admission_failures)
        and any("using RawMutationHandle" in failure for failure in admission_failures)
        and any("struct Mutator" in failure for failure in admission_failures)
        and any("ConnectivityModel &connectivity" in failure for failure in admission_failures)
        and any("missing required declaration" in failure for failure in admission_failures)
        and any("expected exactly 16" in failure for failure in admission_failures),
        "new names, same-name overloads, exposed fields, aliases, nested mutation handles, and "
        "protected access outside the typed contract must fail admission",
    )
    required_circuit_sample = "class Circuit final { public:\n" + ";\n".join(
        sorted(CIRCUIT_REQUIRED_PUBLIC_METHOD_DECLARATIONS)
    ) + ";\n};"
    require_self_test(
        not circuit_public_method_admission_failures(required_circuit_sample),
        "the final 16-declaration Circuit contract must admit its exact required surface",
    )

    missing_final_failures = circuit_public_method_admission_failures(
        required_circuit_sample.replace("class Circuit final", "class Circuit", 1)
    )
    require_self_test(
        any("class Circuit final" in failure for failure in missing_final_failures),
        "Circuit admission must reject a root that is no longer final",
    )
    inherited_circuit_failures = circuit_public_method_admission_failures(
        required_circuit_sample.replace(
            "class Circuit final", "class Circuit final : public CircuitBase", 1
        )
    )
    require_self_test(
        any("no base classes" in failure for failure in inherited_circuit_failures),
        "Circuit admission must reject facade or storage inheritance on the root",
    )

    missing_declaration = "[[nodiscard]] std::optional<NetId> net_of(PinId pin) const"
    missing_declaration_sample = "class Circuit final { public:\n" + ";\n".join(
        sorted(CIRCUIT_REQUIRED_PUBLIC_METHOD_DECLARATIONS - {missing_declaration})
    ) + ";\n};"
    missing_declaration_failures = circuit_public_method_admission_failures(
        missing_declaration_sample
    )
    require_self_test(
        any(
            "missing required declaration" in failure and missing_declaration in failure
            for failure in missing_declaration_failures
        ),
        "Circuit admission must reject an incomplete required declaration set",
    )

    duplicated_declaration = "bool connect(NetId net, PinId pin)"
    duplicated_declaration_sample = "class Circuit final { public:\n" + ";\n".join(
        [*sorted(CIRCUIT_REQUIRED_PUBLIC_METHOD_DECLARATIONS), duplicated_declaration]
    ) + ";\n};"
    duplicated_declaration_failures = circuit_public_method_admission_failures(
        duplicated_declaration_sample
    )
    require_self_test(
        any(
            "duplicates required declaration" in failure and duplicated_declaration in failure
            for failure in duplicated_declaration_failures
        ),
        "Circuit admission must reject duplicated required declarations",
    )

    retired_circuit_surface_sample = "class Circuit final { public:\n" + ";\n".join(
        [
            "[[nodiscard]] ModuleInstanceId instantiate_module(ModuleDefId definition, "
            "ModuleInstanceSpec spec)",
            *sorted(CIRCUIT_LEGACY_PUBLIC_DECLARATIONS),
        ]
    ) + ";\n};"
    retired_surface_failures = circuit_public_method_admission_failures(
        retired_circuit_surface_sample
    )
    retired_declaration_failures = [
        failure
        for failure in retired_surface_failures
        if "retired pre-1.0" in failure or "storage-shaped derived read" in failure
    ]
    require_self_test(
        len(retired_declaration_failures) == len(CIRCUIT_LEGACY_PUBLIC_DECLARATIONS)
        and all(
            any(name in failure for failure in retired_declaration_failures)
            for name in CIRCUIT_STORAGE_SHAPED_READ_NAMES
        )
        and any("instantiate_root_module" in failure for failure in retired_declaration_failures)
        and any(
            "ReferenceDesignator reference" in failure
            for failure in retired_declaration_failures
        ),
        "old construction overloads and every storage-shaped root read must fail admission",
    )
    require_self_test(
        not circuit_contract_configuration_failures(),
        "the final required Circuit declarations must match the 16-entry budget and stay "
        "disjoint from the retired surface",
    )
    legacy_overlap_sample = next(iter(CIRCUIT_LEGACY_PUBLIC_DECLARATIONS))
    overlap_failures = circuit_contract_configuration_failures(
        frozenset({legacy_overlap_sample}), frozenset({legacy_overlap_sample}), 1
    )
    require_self_test(
        any("both required and legacy" in failure for failure in overlap_failures),
        "checker configuration must reject declarations present in both final and legacy sets",
    )

    required_schematic_sample = "class Schematic final { public:\n" + ";\n".join(
        sorted(SCHEMATIC_REQUIRED_PUBLIC_METHOD_DECLARATIONS)
    ) + ";\n};"
    require_self_test(
        not schematic_public_method_admission_failures(required_schematic_sample),
        "the final 18-declaration Schematic contract must admit its exact required surface",
    )
    retired_schematic_sample = "class Schematic final { public:\n" + ";\n".join(
        sorted(SCHEMATIC_LEGACY_PUBLIC_DECLARATIONS)
    ) + ";\n};"
    retired_schematic_failures = schematic_public_method_admission_failures(
        retired_schematic_sample
    )
    require_self_test(
        all(
            any(name in failure for failure in retired_schematic_failures)
            for name in SCHEMATIC_STORAGE_SHAPED_READ_NAMES
        )
        and any("replace_with" in failure for failure in retired_schematic_failures)
        and any("add_terminal_marker" in failure for failure in retired_schematic_failures)
        and any("move_net_label_text" in failure for failure in retired_schematic_failures),
        "retired Schematic storage reads, restoration, aliases, and named moves must fail admission",
    )
    require_self_test(
        not schematic_contract_configuration_failures(),
        "the final required Schematic declarations must match the 18-entry budget and stay "
        "disjoint from the retired surface",
    )

    range_escape_sample = read(ROOT_TYPES["Circuit"]).replace(
        "iterator() = default;",
        "iterator() = default;\n"
        "        static entity_range_t<Id> forge(const Circuit &circuit, std::size_t size);",
        1,
    )
    range_failures = circuit_range_construction_failures(range_escape_sample)
    require_self_test(
        len(range_failures) == 1 and "forge" in range_failures[0],
        "nested iterator factories outside Circuit::all must fail range admission",
    )
    schematic_range_escape_sample = read(ROOT_TYPES["Schematic"]).replace(
        "iterator() = default;",
        "iterator() = default;\n"
        "        static schematic_entity_range_t<Id> forge(const Schematic &schematic, "
        "std::size_t size);",
        1,
    )
    schematic_range_failures = schematic_range_construction_failures(
        schematic_range_escape_sample
    )
    require_self_test(
        len(schematic_range_failures) == 1 and "forge" in schematic_range_failures[0],
        "nested iterator factories outside Schematic::all must fail range admission",
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
    check_no_removed_circuit_architecture(failures)
    check_no_removed_schematic_architecture(failures)
    check_no_kernel_mutation_access(failures)
    check_no_flat_pcb_public_headers(failures)
    check_public_api_evidence(failures)
    check_circuit_public_type_inventory(failures)
    check_no_non_root_circuit_owners(failures)
    failures.extend(circuit_contract_configuration_failures())
    check_circuit_public_method_admission(failures)
    check_circuit_range_construction(failures)
    failures.extend(schematic_contract_configuration_failures())
    check_schematic_public_method_admission(failures)
    check_schematic_range_construction(failures)
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
