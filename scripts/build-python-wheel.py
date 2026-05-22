#!/usr/bin/env python3
"""Build the Python wheel with CMake's selected interpreter."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "dev"
CMAKE_CACHE = BUILD_DIR / "CMakeCache.txt"


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def cmake_cache_value(name: str) -> str:
    for line in CMAKE_CACHE.read_text(encoding="utf-8").splitlines():
        if line.startswith(f"{name}:"):
            return line.split("=", 1)[1]
    raise RuntimeError(f"{name} was not written to {CMAKE_CACHE}")


def selected_python() -> str:
    if not CMAKE_CACHE.exists():
        raise RuntimeError(
            f"{CMAKE_CACHE} does not exist; run python scripts/install-python-dev-deps.py first"
        )

    try:
        return cmake_cache_value("Python3_EXECUTABLE")
    except RuntimeError:
        return cmake_cache_value("_Python3_EXECUTABLE")


def main() -> int:
    run([selected_python(), "-m", "build", "--wheel"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
