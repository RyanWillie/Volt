import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

import volt


SAMPLE = Path(__file__).resolve().parents[2] / "samples" / "5v_led_indicator"
SELECTED_EXPORTS = ("schematic-svg", "board-svg", "bom", "cpl")


def _run(cwd: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    environment = os.environ.copy()
    environment["PYTHONPATH"] = os.pathsep.join(
        str(Path(entry).resolve()) for entry in sys.path if entry
    )
    return subprocess.run(
        [sys.executable, "-m", "volt.cli", *arguments],
        cwd=cwd,
        env=environment,
        check=False,
        capture_output=True,
        text=True,
    )


def _result(completed: subprocess.CompletedProcess[str]) -> dict:
    assert completed.stderr == ""
    return json.loads(completed.stdout)


def _bundle_files(root: Path) -> list[tuple[str, bytes]]:
    return [
        (path.relative_to(root).as_posix(), path.read_bytes())
        for path in sorted(path for path in root.rglob("*") if path.is_file())
    ]


def _artifact_payload(bundle: Path, manifest: dict, kind: str) -> dict:
    [record] = [
        record for record in manifest["artifacts"] if record["id"]["kind"] == kind
    ]
    return json.loads(bundle.joinpath(record["path"]).read_bytes())


def test_led_indicator_public_workflow_is_complete_and_offline(tmp_path: Path) -> None:
    project = tmp_path / "5v_led_indicator"
    shutil.copytree(SAMPLE, project)

    checked = _run(project, "check", "--json")
    assert checked.returncode == 0
    check = _result(checked)
    assert check["status"] == "clean"
    assert check["diagnostics"]["summary"] == {
        "errors": 0,
        "infos": 0,
        "warnings": 0,
    }
    assert check["models"] == {"boards": 1, "logical": 1, "schematics": 1}
    assert check["tests"]["summary"] == {"failed": 0, "passed": 3}

    first = tmp_path / "first.volt"
    second = tmp_path / "second.volt"
    build_arguments = [item for kind in SELECTED_EXPORTS for item in ("--export", kind)]
    built_first = _run(
        project,
        "build",
        "--output",
        str(first),
        *build_arguments,
        "--json",
    )
    built_second = _run(
        project,
        "build",
        "--output",
        str(second),
        *build_arguments,
        "--json",
    )
    assert built_first.returncode == 0
    assert built_second.returncode == 0
    first_result = _result(built_first)
    second_result = _result(built_second)
    assert first_result["status"] == second_result["status"] == "clean"
    assert first_result["selected_export_count"] == 4
    assert first_result["build_id"] == second_result["build_id"]
    assert first_result["bundle_digest"] == second_result["bundle_digest"]
    assert _bundle_files(first) == _bundle_files(second)

    manifest = json.loads(first.joinpath("manifest.volt.json").read_bytes())
    selected_parts = manifest["dependency_lock"]["selected_parts"]
    assert [item["selected_part"]["part_key"] for item in selected_parts] == [
        "LTST-C190KRKT",
        "MOLEX-22-28-4020",
        "RC0603FR-07330RL",
    ]
    assert {item["selected_part"]["library_namespace"] for item in selected_parts} == {
        "volt.samples.5v_led_indicator"
    }
    assert len({item["selected_part"]["part_digest"] for item in selected_parts}) == 3

    logical = _artifact_payload(first, manifest, "logical_model")
    components = {item["id"]: item["reference"] for item in logical["components"]}
    pins = {item["id"]: components[item["component"]] for item in logical["pins"]}
    assert {
        net["name"]: tuple(pins[pin] for pin in net["pins"]) for net in logical["nets"]
    } == {
        "+5V": ("J1", "R1"),
        "LED_A": ("R1", "D1"),
        "GND": ("D1", "J1"),
    }
    five_volts = next(net for net in logical["nets"] if net["name"] == "+5V")
    assert five_volts["electrical_attributes"]["voltage"]["value"] == 5.0

    board = _artifact_payload(first, manifest, "board_model")["board"]
    assert board["name"] == "Indicator"
    assert board["outline"]["vertices"] == [[0, 0], [28, 0], [28, 14], [0, 14]]
    assert [
        layer["name"] for layer in board["layers"] if layer["role"] == "copper"
    ] == ["F.Cu", "B.Cu"]
    assert len(board["placements"]) == 3
    assert {track["net"] for track in board["tracks"]} == {
        net["id"] for net in logical["nets"]
    }

    diagnostics = _artifact_payload(first, manifest, "diagnostics")
    assert diagnostics["diagnostics"] == []
    assert not any(
        diagnostic["code"] == "PCB_NET_UNROUTED"
        for diagnostic in diagnostics["diagnostics"]
    )

    opened = volt.ProjectBundle.open(first)
    loaded = opened.graph.loaded_project
    assert len(loaded.circuits) == 1
    assert len(loaded.schematics) == 1
    assert len(loaded.boards) == 1
    assert len(loaded.compiled_boards) == 1
    assert len(loaded.board_scenes) == 1
    compiled = loaded.compiled_boards[0]
    scene = loaded.board_scenes[0]
    assert scene.compiled_board.identity == compiled.identity
    with pytest.raises(AttributeError):
        compiled.identity = {}
    with pytest.raises(AttributeError):
        scene.compiled_board = {}

    project.joinpath("main.py").write_text(
        "raise RuntimeError('offline commands executed project source')\n",
        encoding="utf-8",
    )

    inspected = _run(project, "inspect", "--bundle", str(first), "--json")
    assert inspected.returncode == 0
    inspection = _result(inspected)
    assert inspection["integrity"] == "verified-v2"
    assert inspection["project"]["name"] == "5v-led-indicator"
    assert [item["name"] for item in inspection["boards"]] == ["Indicator"]
    assert len(inspection["compiled_boards"]) == 1
    assert len(inspection["scenes"]) == 1
    assert len(inspection["parts"]) == 3
    assert {item["kind"] for item in inspection["selected_exports"]} == {
        "schematic_svg",
        "bom",
        "board_svg",
        "cpl",
    }

    offline = tmp_path / "offline"
    exported = _run(
        project,
        "export",
        "--bundle",
        str(first),
        "--output",
        str(offline),
        "--json",
    )
    assert exported.returncode == 0
    assert {item["kind"] for item in _result(exported)["artifacts"]} == {
        "schematic_svg",
        "bom",
        "board_svg",
        "cpl",
    }

    identical = _run(project, "diff", str(first), str(second), "--json")
    assert identical.returncode == 0
    assert _result(identical)["status"] == "identical"
