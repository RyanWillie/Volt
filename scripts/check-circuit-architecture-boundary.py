#!/usr/bin/env python3
"""Check Volt kernel architecture boundaries that should not regress."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CODE_DIRS = [ROOT / "include", ROOT / "src", ROOT / "tests", ROOT / "examples", ROOT / "python"]
ALLOWLIST_DIR = ROOT / "tests" / "architecture"

ROOT_TYPES = {
    "Circuit": ROOT / "include" / "volt" / "circuit" / "circuit.hpp",
    "Board": ROOT / "include" / "volt" / "pcb" / "board.hpp",
    "Schematic": ROOT / "include" / "volt" / "schematic" / "schematic.hpp",
}

REJECTED_TOKENS = (
    "CircuitView",
    "CircuitHierarchy",
    "CircuitElectrical",
    "CircuitDesignIntent",
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
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


def check_rejected_tokens(failures: list[str]) -> None:
    for path in code_files():
        text = read(path)
        for token in REJECTED_TOKENS:
            if token in text:
                fail(f"{path.relative_to(ROOT)} contains rejected architecture token {token}", failures)


def check_circuit_has_no_friend_classes(failures: list[str]) -> None:
    body = class_body(strip_comments(read(ROOT_TYPES["Circuit"])), "Circuit")
    if re.search(r"\bfriend\s+class\b", body):
        fail("include/volt/circuit/circuit.hpp declares a friend class inside Circuit", failures)


def check_public_api_snapshots(failures: list[str]) -> None:
    for class_name, header_path in ROOT_TYPES.items():
        allowlist = ALLOWLIST_DIR / f"{class_name.lower()}_public_api.txt"
        if not allowlist.exists():
            fail(f"{allowlist.relative_to(ROOT)} is missing", failures)
            continue

        actual = public_api_snapshot(class_name, header_path)
        expected = [
            line.rstrip()
            for line in read(allowlist).splitlines()
            if not line.startswith("#")
        ]
        if actual != expected:
            fail(
                f"{allowlist.relative_to(ROOT)} does not match {header_path.relative_to(ROOT)}",
                failures,
            )


def subsystem_root_name(path: Path) -> str | None:
    name = path.name
    if path.match("include/volt/circuit/*"):
        if name in {"circuit.hpp", "validation.hpp", "queries.hpp"}:
            return None
        if "_model" in name or name in {"design_intent.hpp", "net_classes.hpp"}:
            return "Circuit"
    if path.match("include/volt/pcb/*"):
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
            fail(f"{path.relative_to(ROOT)} holds or accepts a {root_name} back-reference", failures)


def check_subsystem_sources_have_real_logic(failures: list[str]) -> None:
    source_patterns = [
        ROOT / "src" / "circuit",
        ROOT / "src" / "pcb",
        ROOT / "src" / "schematic",
    ]
    for directory in source_patterns:
        if not directory.exists():
            continue
        for path in directory.glob("*"):
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
                fail(f"{path.relative_to(ROOT)} delegates through a model root back-reference", failures)
            meaningful_lines = [
                line.strip()
                for line in text.splitlines()
                if line.strip()
                and not line.strip().startswith("#include")
                and line.strip() not in {"namespace volt {", "} // namespace volt", "}"}
            ]
            if len(meaningful_lines) < 4:
                fail(f"{path.relative_to(ROOT)} does not contain enough real subsystem logic", failures)


def main() -> int:
    failures: list[str] = []
    check_rejected_tokens(failures)
    check_circuit_has_no_friend_classes(failures)
    check_public_api_snapshots(failures)
    check_subsystem_back_references(failures)
    check_subsystem_sources_have_real_logic(failures)

    if failures:
        for failure in failures:
            print(f"architecture boundary violation: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
