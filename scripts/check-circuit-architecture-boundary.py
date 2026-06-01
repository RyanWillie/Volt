#!/usr/bin/env python3
"""Check logical circuit ownership boundaries across projection and adapter layers."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(path: str) -> str:
    file_path = ROOT / path
    require(file_path.exists(), f"{path} must exist")
    return file_path.read_text(encoding="utf-8")


def source_files(*roots: str) -> list[Path]:
    files: list[Path] = []
    for root in roots:
        base = ROOT / root
        require(base.exists(), f"{root} must exist")
        files.extend(
            path
            for path in base.rglob("*")
            if path.suffix in {".cpp", ".hpp"} and path.is_file()
        )
    return sorted(files)


def check_cmake_dependency_direction() -> None:
    circuit_cmake = read("src/circuit/CMakeLists.txt")
    schematic_cmake = read("src/schematic/CMakeLists.txt")
    pcb_cmake = read("src/pcb/CMakeLists.txt")
    kicad_cmake = read("src/adapters/kicad/CMakeLists.txt")

    for forbidden in ("Volt::Schematic", "Volt::PCB", "Volt::IO", "Volt::KiCadAdapter"):
        require(forbidden not in circuit_cmake, f"Volt::Circuit must not depend on {forbidden}")

    require("Volt::Circuit" in schematic_cmake, "Volt::Schematic must depend on Volt::Circuit")
    for forbidden in ("Volt::PCB", "Volt::IO", "Volt::KiCadAdapter"):
        require(forbidden not in schematic_cmake, f"Volt::Schematic must not depend on {forbidden}")

    require("Volt::Circuit" in pcb_cmake, "Volt::PCB must depend on Volt::Circuit")
    for forbidden in ("Volt::Schematic", "Volt::IO", "Volt::KiCadAdapter"):
        require(forbidden not in pcb_cmake, f"Volt::PCB must not depend on {forbidden}")

    require("Volt::Circuit" in kicad_cmake, "KiCad adapter must consume Volt::Circuit")
    require("Volt::Schematic" in kicad_cmake, "KiCad adapter must consume Volt::Schematic")
    require("Volt::IO" not in kicad_cmake, "KiCad adapter must not depend on Volt::IO")
    require("Volt::PCB" not in kicad_cmake, "KiCad adapter must not depend on Volt::PCB")


def check_view_contracts() -> None:
    circuit = read("include/volt/circuit/circuit.hpp")
    schematic = read("include/volt/schematic/schematic.hpp")
    schematic_document = read("include/volt/schematic/schematic_document.hpp")
    board = read("include/volt/pcb/board.hpp")

    require("[[nodiscard]] CircuitView view() const noexcept;" in circuit,
            "Circuit must expose CircuitView for read-only consumers")
    require("operator CircuitView() const noexcept;" in circuit,
            "Circuit must convert to CircuitView for existing read-only call sites")

    require("CircuitView circuit_;" in schematic, "Schematic must store CircuitView, not Circuit")
    require("CircuitView circuit() const noexcept" in schematic,
            "Schematic must expose its logical circuit as CircuitView")
    require("CircuitView circuit() const noexcept" in schematic_document,
            "SchematicDocument must expose its logical circuit as CircuitView")

    require("CircuitView circuit_;" in board, "Board must store CircuitView, not Circuit")
    require("CircuitView circuit() const noexcept" in board,
            "Board must expose its logical circuit as CircuitView")

    require("CircuitView" in read("include/volt/circuit/validation.hpp"),
            "Circuit validation must consume CircuitView")
    require("CircuitView" in read("include/volt/io/schematic_reader.hpp"),
            "Schematic reader must consume CircuitView")
    require("CircuitView" in read("include/volt/io/pcb_reader.hpp"),
            "PCB reader must consume CircuitView")


def check_projections_do_not_mutate_logical_circuit() -> None:
    files = source_files(
        "include/volt/schematic",
        "include/volt/pcb",
        "include/volt/adapters",
        "src/schematic",
        "src/pcb",
        "src/adapters",
    )
    forbidden_patterns = (
        (re.compile(r"#include <volt/circuit/circuit\.hpp>"),
         "projection and adapter layers should include circuit_view.hpp, not circuit.hpp"),
        (re.compile(r"#include <volt/circuit/(hierarchy|electrical|design_intent)_mutations\.hpp>"),
         "projection and adapter layers must not import logical mutation facades"),
        (re.compile(r"\bCircuit(?:Hierarchy|Electrical|DesignIntent)\b"),
         "projection and adapter layers must not construct logical mutation facades"),
        (re.compile(r"\b(?:circuit|circuit_|logical_circuit|logical)\s*(?:\.|->)\s*"
                    r"(?:add_pin_definition|add_component_definition|add_component|add_pin|"
                    r"add_net|instantiate_component|connect|disconnect)\s*\("),
         "projection and adapter layers must not mutate logical circuit entities or nets"),
    )

    violations: list[str] = []
    for path in files:
        relative = path.relative_to(ROOT)
        text = path.read_text(encoding="utf-8")
        for pattern, message in forbidden_patterns:
            for match in pattern.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                violations.append(f"{relative}:{line}: {message}")

    if violations:
        raise AssertionError("\n  ".join(violations))


def main() -> int:
    failures = []
    for check in (
        check_cmake_dependency_direction,
        check_view_contracts,
        check_projections_do_not_mutate_logical_circuit,
    ):
        try:
            check()
        except AssertionError as exc:
            failures.append(str(exc))

    if failures:
        for failure in failures:
            print(f"circuit-architecture-boundary check failed: {failure}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
