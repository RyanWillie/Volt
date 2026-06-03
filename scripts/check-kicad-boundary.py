#!/usr/bin/env python3
"""Check that KiCad adapter dependencies point toward Volt, never back into core."""

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


def check_adapter_target() -> None:
    top_level = read("CMakeLists.txt")
    adapter_cmake = read("src/adapters/kicad/CMakeLists.txt")

    require(
        "add_subdirectory(src/adapters/kicad)" in top_level,
        "top-level CMake must add the KiCad adapter target explicitly",
    )
    require(
        re.search(r"add_library\s*\(\s*volt_kicad_adapter\b", adapter_cmake) is not None,
        "KiCad adapter target must exist",
    )
    require("add_library(Volt::KiCadAdapter" in adapter_cmake, "KiCad adapter alias must exist")
    require("Volt::Circuit" in adapter_cmake, "KiCad adapter must depend on Volt::Circuit")
    require("Volt::PCB" in adapter_cmake, "KiCad PCB export must depend on Volt::PCB")
    require("Volt::Schematic" in adapter_cmake, "KiCad adapter must depend on Volt::Schematic")


def check_core_has_no_kicad_dependency() -> None:
    core_paths = (
        "src/core/CMakeLists.txt",
        "src/circuit/CMakeLists.txt",
        "src/authoring/CMakeLists.txt",
        "src/schematic/CMakeLists.txt",
        "src/io/CMakeLists.txt",
        "include/volt/volt.hpp",
    )

    violations = [
        f"{path} must not mention KiCad adapter types or targets"
        for path in core_paths
        if "kicad" in read(path).lower()
    ]

    if violations:
        raise AssertionError("\n  ".join(violations))


def main() -> int:
    failures = []
    for check in (check_adapter_target, check_core_has_no_kicad_dependency):
        try:
            check()
        except AssertionError as exc:
            failures.append(str(exc))

    if failures:
        for failure in failures:
            print(f"kicad-boundary check failed: {failure}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
