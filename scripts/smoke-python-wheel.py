#!/usr/bin/env python3
"""Install the built Python wheel and smoke-test the public import."""

from __future__ import annotations

from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "dev"
CMAKE_CACHE = BUILD_DIR / "CMakeCache.txt"
DIST_DIR = ROOT / "dist"
SMOKE_VENV = BUILD_DIR / "wheel-smoke-venv"


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


def built_wheel() -> Path:
    wheels = sorted(DIST_DIR.glob("*.whl"))
    if len(wheels) != 1:
        raise RuntimeError(f"Expected exactly one wheel in {DIST_DIR}, found {len(wheels)}")
    return wheels[0]


def venv_python() -> str:
    if SMOKE_VENV.exists():
        shutil.rmtree(SMOKE_VENV)

    run([selected_python(), "-m", "venv", str(SMOKE_VENV)])
    if os.name == "nt":
        return str(SMOKE_VENV / "Scripts" / "python.exe")
    return str(SMOKE_VENV / "bin" / "python")


def main() -> int:
    python = venv_python()
    run([python, "-m", "pip", "install", "--force-reinstall", str(built_wheel())])
    run(
        [
            python,
            "-c",
            (
                "import volt; "
                "design = volt.Design('wheel_smoke'); "
                "assert design.name == 'wheel_smoke'"
            ),
        ]
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
