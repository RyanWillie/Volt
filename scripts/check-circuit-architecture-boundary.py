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

PRIVILEGED_FRIEND_ALLOWLIST = {
    (
        "include/volt/circuit/intent/design_intent.hpp",
        "friend class Circuit",
    ): "Circuit aggregate root currently owns design-intent mutations; VOL-232 will narrow this.",
    (
        "include/volt/circuit/electrical/electrical_model.hpp",
        "friend class Circuit",
    ): "Circuit aggregate root currently owns electrical metadata mutations; VOL-232 will narrow this.",
    (
        "include/volt/circuit/hierarchy/hierarchy.hpp",
        "friend class HierarchyModel",
    ): "HierarchyModel currently maintains ModuleDefinition membership; VOL-232 will replace this.",
    (
        "include/volt/circuit/hierarchy/hierarchy_model.hpp",
        "friend class Circuit",
    ): "Circuit aggregate root currently owns hierarchy mutations; VOL-232 will narrow this.",
    (
        "include/volt/circuit/connectivity/instances.hpp",
        "friend class ConnectivityModel",
    ): "ConnectivityModel currently updates instance properties at the mutation boundary.",
    (
        "include/volt/circuit/constraints/net_classes.hpp",
        "friend class Circuit",
    ): "Circuit aggregate root currently owns net-class assignment mutations; VOL-232 will narrow this.",
    (
        "include/volt/circuit/parts/parts.hpp",
        "friend class ElectricalModel",
    ): "ElectricalModel currently applies selected-part electrical attributes; VOL-232 will narrow this.",
    (
        "include/volt/pcb/copper/board_copper_model.hpp",
        "friend class Board",
    ): "Board aggregate root currently owns copper mutations; VOL-232 will narrow this.",
    (
        "include/volt/pcb/placement/board_placement_model.hpp",
        "friend class Board",
    ): "Board aggregate root currently owns placement mutations; VOL-232 will narrow this.",
    (
        "include/volt/pcb/routing/board_spatial_index.hpp",
        "friend class BoardRouter",
    ): "BoardRouter currently inserts accepted transient shapes into its routing index.",
    (
        "include/volt/pcb/routing/board_spatial_index.hpp",
        "friend void detail::validate_copper_clearance(const Board &board, const std::vector<detail::BoardCopperShape> &shapes, DiagnosticReport &report)",
    ): "Board copper DRC currently reuses the spatial index's internal shape snapshot.",
    (
        "include/volt/schematic/schematic_items_model.hpp",
        "friend class Schematic",
    ): "Schematic aggregate root currently owns item mutations; VOL-232 will narrow this.",
    (
        "include/volt/schematic/schematic_sheet.hpp",
        "friend class SchematicSheetModel",
    ): "SchematicSheetModel currently maintains sheet membership lists; VOL-232 will narrow this.",
    (
        "include/volt/schematic/schematic_sheet_model.hpp",
        "friend class Schematic",
    ): "Schematic aggregate root currently owns sheet membership mutations; VOL-232 will narrow this.",
}

PYTHON_CONNECTIVITY_SEMANTICS_ALLOWLIST = {
    (
        "python/volt/_pcb_composition.py",
        "fanout",
        "route_net = net if net is not None else context.pad_net(source)",
    ): "Existing PCB fanout helper infers route nets from pad anchors; VOL-234 moves this into the kernel.",
    (
        "python/volt/_pcb_composition.py",
        "fanout",
        "if route_net is None:",
    ): "Existing PCB fanout helper validates Python-inferred route nets; VOL-234 moves this into the kernel.",
    (
        "python/volt/_pcb_composition.py",
        "_route_net",
        "start_net = context.pad_net(start)",
    ): "Known Python route-net inference choke point scheduled for kernel ownership in VOL-234.",
    (
        "python/volt/_pcb_composition.py",
        "_route_net",
        "end_net = context.pad_net(end)",
    ): "Known Python route-net inference choke point scheduled for kernel ownership in VOL-234.",
    (
        "python/volt/_pcb_composition.py",
        "_route_net",
        "if start_net is None or end_net is None:",
    ): "Known Python route-net inference choke point scheduled for kernel ownership in VOL-234.",
    (
        "python/volt/_pcb_composition.py",
        "_route_net",
        "if start_net != end_net:",
    ): "Known Python route-net inference choke point scheduled for kernel ownership in VOL-234.",
    (
        "python/volt/_pcb_layout.py",
        "BoardLayout._pad_anchor_net",
        "return resolution.net",
    ): "Known Python pad-anchor net resolver scheduled for kernel ownership in VOL-234.",
}

ENTITY_REF_KERNEL_ALLOWLIST = {
    (
        "src/pcb/copper/board_copper.cpp",
        "[kind](EntityRef entity) { return entity.kind() == kind; });",
    ): "Existing DRC helper classifies diagnostic copper shapes by reporting refs; keep narrow until a typed shape role replaces it.",
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


def relative(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def fail(message: str, failures: list[str]) -> None:
    failures.append(message)


def class_body(header: str, class_name: str) -> str:
    match = re.search(rf"\bclass\s+{re.escape(class_name)}\b[^{{]*{{", header)
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


def is_privileged_friend(declaration: str) -> bool:
    return declaration.startswith("friend ") and not is_comparison_operator_friend(declaration)


def declaration_chunks(section: str) -> list[str]:
    section = re.sub(r"(\)\s*(?:const\s*)?(?:noexcept\s*)?)\{[^{}]*\}", r"\1;", section)
    return [normalize_declaration(chunk) for chunk in section.split(";")]


def public_declarations(class_name: str, header_path: Path) -> list[str]:
    body = class_body(strip_comments(read(header_path)), class_name)
    declarations: list[str] = []
    for chunk in declaration_chunks(public_section(body)):
        if "(" not in chunk or ")" not in chunk:
            continue
        prefix = chunk.split("(", 1)[0].strip()
        if not prefix:
            continue
        name = prefix.split()[-1].split("::")[-1].strip("&*")
        if name in {"if", "for", "while", "switch"}:
            continue
        declarations.append(chunk)
    return declarations


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


def check_rejected_tokens(failures: list[str]) -> None:
    for path in code_files():
        text = read(path)
        for token in REJECTED_TOKENS:
            if token in text:
                fail(f"{relative(path)} contains rejected architecture token {token}", failures)


def check_circuit_has_no_friend_classes(failures: list[str]) -> None:
    body = class_body(strip_comments(read(ROOT_TYPES["Circuit"])), "Circuit")
    if re.search(r"\bfriend\s+class\b", body):
        fail("include/volt/circuit/circuit.hpp declares a friend class inside Circuit", failures)


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
    for class_name, header_path in ROOT_TYPES.items():
        allowlist = ALLOWLIST_DIR / f"{class_name.lower()}_public_api.txt"
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


def require_self_test(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_self_tests() -> int:
    friend_sample = textwrap.dedent(
        """
        struct Comparable {
            [[nodiscard]] friend bool operator==(const Comparable &lhs,
                                                 const Comparable &rhs) noexcept {
                return lhs.value == rhs.value;
            }
            friend class Backdoor;
            friend void mutate_private_state(Comparable &value);
            int value = 0;
        };
        """
    )
    friends = friend_declarations(Path("sample.hpp"), friend_sample)
    require_self_test(
        any(is_comparison_operator_friend(item.declaration) for item in friends),
        "comparison operator friends must be classified as non-privileged value semantics",
    )
    privileged = [item for item in friends if is_privileged_friend(item.declaration)]
    require_self_test(
        [item.declaration for item in privileged]
        == ["friend class Backdoor", "friend void mutate_private_state(Comparable &value)"],
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
    check_circuit_has_no_friend_classes(failures)
    check_no_flat_pcb_public_headers(failures)
    check_privileged_friends_are_allowlisted(failures)
    check_public_api_snapshots(failures)
    check_python_connectivity_semantics(failures)
    check_entity_ref_not_kernel_traversal_handle(failures)
    check_subsystem_back_references(failures)
    check_subsystem_sources_have_real_logic(failures)

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
