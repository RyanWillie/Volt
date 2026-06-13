import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_kicad_fab_verify_script_skips_when_kicad_cli_is_absent(tmp_path):
    board = tmp_path / "board.kicad_pcb"
    board.write_text("(kicad_pcb\n)\n", encoding="utf-8")
    empty_bin = tmp_path / "bin"
    empty_bin.mkdir()
    env = {**os.environ, "PATH": str(empty_bin)}

    result = subprocess.run(
        [
            sys.executable,
            "scripts/verify-kicad-fab-export.py",
            str(board),
            "--output-dir",
            str(tmp_path / "fab"),
        ],
        cwd=ROOT,
        env=env,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    assert result.returncode == 0
    assert "kicad-cli not found; skipping KiCad fabrication verification" in result.stdout


def test_kicad_fab_verify_script_rejects_missing_board_before_optional_skip(tmp_path):
    empty_bin = tmp_path / "bin"
    empty_bin.mkdir()
    missing_board = tmp_path / "missing.kicad_pcb"
    env = {**os.environ, "PATH": str(empty_bin)}

    result = subprocess.run(
        [
            sys.executable,
            "scripts/verify-kicad-fab-export.py",
            str(missing_board),
        ],
        cwd=ROOT,
        env=env,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    assert result.returncode == 2
    assert "does not exist or is not a file" in result.stderr
