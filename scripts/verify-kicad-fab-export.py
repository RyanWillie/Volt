#!/usr/bin/env python3
"""Optionally verify a Volt-exported KiCad PCB handoff with kicad-cli."""

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Iterable


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", type=Path, help="Path to the exported .kicad_pcb file")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Directory for generated Gerber, drill, and optional DRC outputs",
    )
    parser.add_argument(
        "--drc",
        action="store_true",
        help="Also run kicad-cli pcb drc and write drc.rpt into the output directory",
    )
    return parser.parse_args(tuple(argv))


def run(command: list[str]) -> None:
    print("$ " + " ".join(command))
    subprocess.run(command, check=True)


def main(argv: Iterable[str] = sys.argv[1:]) -> int:
    args = parse_args(argv)
    board = args.board
    if not board.is_file():
        print(f"{board} does not exist or is not a file", file=sys.stderr)
        return 2

    kicad_cli = shutil.which("kicad-cli")
    if kicad_cli is None:
        print("kicad-cli not found; skipping KiCad fabrication verification")
        return 0

    output_dir = args.output_dir or board.with_suffix("").with_name(f"{board.stem}-kicad-fab")
    gerber_dir = output_dir / "gerbers"
    drill_dir = output_dir / "drill"
    gerber_dir.mkdir(parents=True, exist_ok=True)
    drill_dir.mkdir(parents=True, exist_ok=True)

    run([kicad_cli, "pcb", "export", "gerbers", "--output", str(gerber_dir), str(board)])
    run([kicad_cli, "pcb", "export", "drill", "--output", str(drill_dir), str(board)])
    if args.drc:
        run([kicad_cli, "pcb", "drc", "--output", str(output_dir / "drc.rpt"), str(board)])

    print(f"KiCad fabrication verification outputs written to {output_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
