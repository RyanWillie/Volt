#!/usr/bin/env python3
"""Install Python dev dependencies into CMake's selected interpreter."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "dev"
CMAKE_CACHE = BUILD_DIR / "CMakeCache.txt"
REQUIREMENTS = ROOT / "requirements-dev.txt"


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def cmake_cache_value(name: str) -> str:
    for line in CMAKE_CACHE.read_text(encoding="utf-8").splitlines():
        if line.startswith(f"{name}:"):
            return line.split("=", 1)[1]
    raise RuntimeError(f"{name} was not written to {CMAKE_CACHE}")


def install_requirements(python: str) -> None:
    command = [python, "-m", "pip", "install", "-r", str(REQUIREMENTS)]
    print("+", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode == 0:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return

    if "externally-managed-environment" not in result.stderr:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        result.check_returncode()

    print("Detected externally managed Python; retrying as a user install.", file=sys.stderr)
    run(command[:4] + ["--user", "--break-system-packages"] + command[4:])


def main() -> int:
    if not REQUIREMENTS.exists():
        raise RuntimeError(f"{REQUIREMENTS} does not exist")

    run(["cmake", "--preset", "dev", "-DVOLT_BUILD_TESTS=OFF"])
    try:
        python = cmake_cache_value("Python3_EXECUTABLE")
    except RuntimeError:
        python = cmake_cache_value("_Python3_EXECUTABLE")

    install_requirements(python)
    run([python, "-c", "import pytest"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
