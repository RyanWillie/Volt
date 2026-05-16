#!/usr/bin/env python3
"""Install Volt's tracked Git hooks for this checkout."""

import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    subprocess.run(["git", "config", "core.hooksPath", ".githooks"], cwd=ROOT, check=True)
    print("Configured Git hooks path: .githooks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
