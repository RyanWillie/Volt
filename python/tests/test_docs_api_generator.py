from __future__ import annotations

from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts" / "generate-python-api-docs.py"


def run_generator(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(GENERATOR), *args],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def test_api_generator_emits_mintlify_reference_pages(tmp_path):
    output = tmp_path / "api" / "python"
    navigation = tmp_path / "api-navigation.json"

    result = run_generator("--output", str(output), "--navigation", str(navigation))

    assert result.returncode == 0, result.stderr
    design = (output / "design.mdx").read_text(encoding="utf-8")
    assert "title: Design" in design
    assert "`volt.Design`" in design
    assert "Source: `python/volt/design.py`" in design
    assert "net(self, name: str" in design
    assert "validate(self) -> DiagnosticReport" in design

    index = (output / "index.mdx").read_text(encoding="utf-8")
    assert "Generated from `python/volt/__init__.py`" in index
    assert "/api/python/design" in index
    assert "/api/python/schematic" in index

    navigation_text = navigation.read_text(encoding="utf-8")
    assert '"api/python/index"' in navigation_text
    assert '"api/python/design"' in navigation_text
    assert '"api/python/pcb"' in navigation_text


def test_api_generator_check_mode_detects_stale_output(tmp_path):
    output = tmp_path / "api" / "python"
    navigation = tmp_path / "api-navigation.json"

    result = run_generator("--output", str(output), "--navigation", str(navigation))
    assert result.returncode == 0, result.stderr

    design_path = output / "design.mdx"
    design_path.write_text(
        design_path.read_text(encoding="utf-8") + "\nManual drift.\n",
        encoding="utf-8",
    )

    check = run_generator(
        "--check",
        "--output",
        str(output),
        "--navigation",
        str(navigation),
    )

    assert check.returncode == 1
    assert "generated API docs are stale" in check.stderr
    assert "design.mdx" in check.stderr
