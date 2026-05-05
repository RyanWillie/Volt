#!/usr/bin/env python3
"""Check clang-format compliance for Volt C++ sources."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = ["include", "src", "tests", "examples"]
SUFFIXES = {".cpp", ".hpp"}

files = [
    path
    for directory in SOURCE_DIRS
    for path in (ROOT / directory).rglob("*")
    if path.suffix in SUFFIXES
]

if not files:
    sys.exit(0)

command = ["clang-format", "--dry-run", "--Werror", *map(str, files)]
result = subprocess.run(command, cwd=ROOT, check=False)
sys.exit(result.returncode)
