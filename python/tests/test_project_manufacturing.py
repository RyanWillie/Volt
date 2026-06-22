import json
import sys
import zipfile
from pathlib import Path

import pytest

import volt
from test_cli_manufacturing import (
    _directory_bytes,
    _read_stdout_json,
    _write_entrypoint,
    _write_manufacturing_project,
    _write_project,
)
from volt.cli import load_project_config, main, run_entrypoint


def _run_fixture_project(root: Path) -> volt.ProjectResult:
    value = run_entrypoint(load_project_config(root / "volt.toml"))
    assert isinstance(value, volt.ProjectResult)
    return value


def _fixture_profile_metadata(root: Path) -> dict[str, str]:
    return {
        "path": "profiles/generic.volt.json",
        "resolved_path": str(root / "profiles" / "generic.volt.json"),
    }


def test_project_result_write_manufacturing_package_matches_cli_layout(
    tmp_path, capsys
):
    root = tmp_path / "board"
    api_output = tmp_path / "api-package"
    cli_output = tmp_path / "cli-package"
    _write_manufacturing_project(root)

    result = _run_fixture_project(root)
    package = result.write_manufacturing_package(
        api_output,
        archive=False,
        manufacturing_profile=_fixture_profile_metadata(root),
    )
    assert package.output == api_output
    assert package.archive is None
    assert package.board == {
        "design": "status-led",
        "name": "Control",
        "output_name": "Control",
    }
    assert package.native_fabrication["coverage"] == {
        "classification": "complete",
        "fab_critical_loss": False,
    }

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--json",
                "--output",
                str(cli_output),
                "--project",
                str(root),
            ]
        )
        == 0
    )
    _read_stdout_json(capsys)

    assert _directory_bytes(api_output) == _directory_bytes(cli_output)
    manifest = json.loads(
        api_output.joinpath("manufacturing", "manifest.json").read_text(
            encoding="utf-8"
        )
    )
    assert manifest["format"] == "volt.manufacturing_package"
    assert manifest["profile"]["config"] == _fixture_profile_metadata(root)


def test_project_result_write_manufacturing_package_is_deterministic(
    tmp_path,
):
    root = tmp_path / "board"
    output = tmp_path / "manufacturing-package"
    _write_manufacturing_project(root)

    result = _run_fixture_project(root)
    package = result.write_manufacturing_package(
        output,
        archive=True,
        manufacturing_profile=_fixture_profile_metadata(root),
    )
    assert package.archive == output.with_suffix(".zip")

    first = _directory_bytes(output)
    first_archive = package.archive.read_bytes()
    output.joinpath("stale-order-file.txt").write_text("stale", encoding="utf-8")
    output.joinpath("manufacturing", "old-gerber.gbr").write_text(
        "stale",
        encoding="utf-8",
    )

    second_package = result.write_manufacturing_package(
        output,
        archive=True,
        manufacturing_profile=_fixture_profile_metadata(root),
    )

    assert _directory_bytes(output) == first
    assert second_package.archive == package.archive
    assert second_package.archive.read_bytes() == first_archive
    with zipfile.ZipFile(second_package.archive) as archive:
        assert "stale-order-file.txt" not in archive.namelist()
        assert "manufacturing/old-gerber.gbr" not in archive.namelist()


def test_project_result_write_manufacturing_package_refuses_fab_critical_loss(
    tmp_path,
):
    root = tmp_path / "board"
    output = tmp_path / "manufacturing-package"
    _write_manufacturing_project(root, lossy=True)

    result = _run_fixture_project(root)
    with pytest.raises(
        volt.ManufacturingPackageError,
        match="native fabrication export reported fab-critical loss",
    ) as failure:
        result.write_manufacturing_package(output)

    assert failure.value.status == "native-fabrication-loss"
    assert failure.value.native_fabrication["coverage"] == {
        "classification": "fab-critical-loss",
        "fab_critical_loss": True,
    }
    assert not output.exists()


def test_project_result_write_manufacturing_package_reports_selector_errors_without_writing(
    tmp_path,
):
    root = tmp_path / "board"
    output = tmp_path / "manufacturing-package"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def main():
    project = volt.Project("control-panel")

    @project.design
    def design():
        return (volt.Design("main-controller"), volt.Design("front-panel"))

    @project.board
    def board(context):
        boards = []
        for design in context.designs:
            board = design.board("Main")
            board.set_rectangular_outline(origin=(0, 0), size=(20, 10))
            boards.append(board)
        return tuple(boards)

    return project.run()
""",
    )

    result = _run_fixture_project(root)
    with pytest.raises(LookupError, match="multiple boards"):
        result.write_manufacturing_package(output)
    assert not output.exists()

    with pytest.raises(LookupError, match="No board named 'missing'"):
        result.write_manufacturing_package(output, board="missing")
    assert not output.exists()

    sys.modules.pop("project_entry", None)
