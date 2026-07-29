import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

from volt.cli import CliError, _handle_export
from volt.cli._project_worker import _board


FIXTURES = Path(__file__).parent / "fixtures" / "project_cli"


def _copy_fixture(tmp_path: Path, name: str) -> Path:
    destination = tmp_path / name
    shutil.copytree(FIXTURES / name, destination)
    return destination


def _run(
    cwd: Path,
    *arguments: str,
    environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy() if environment is None else environment.copy()
    env["PYTHONPATH"] = os.pathsep.join(
        str(Path(entry).resolve()) for entry in sys.path if entry
    )
    return subprocess.run(
        [sys.executable, "-m", "volt.cli", *arguments],
        cwd=cwd,
        env=env,
        check=False,
        capture_output=True,
        text=True,
    )


def _json(completed: subprocess.CompletedProcess[str]) -> dict:
    assert completed.stderr == ""
    return json.loads(completed.stdout)


def _source_execution_count(sentinel: Path) -> int:
    if not sentinel.exists():
        return 0
    return len(sentinel.read_text(encoding="utf-8").splitlines())


def test_verified_single_board_workflow_executes_source_only_for_check_and_build(
    tmp_path,
):
    project = _copy_fixture(tmp_path, "single_board")
    sentinel = tmp_path / "source-executions.txt"
    environment = os.environ.copy()
    environment["VOLT_TEST_SOURCE_SENTINEL"] = str(sentinel)

    checked = _run(
        project,
        "check",
        "--project",
        str(project),
        "--json",
        environment=environment,
    )
    assert checked.returncode == 0
    assert len(checked.stdout.splitlines()) == 1
    check = _json(checked)
    assert check["format"] == "volt.cli-result"
    assert check["schema_version"] == 1
    assert check["command"] == "check"
    assert check["ok"] is True
    assert check["structurally_valid"] is True
    assert check["target_ready"] is True
    assert check["tests"]["summary"] == {"failed": 0, "passed": 0}
    assert _source_execution_count(sentinel) == 1

    default_bundle = tmp_path / "default.volt"
    built_default = _run(
        project,
        "build",
        "--project",
        str(project),
        "--output",
        str(default_bundle),
        "--json",
        environment=environment,
    )
    assert built_default.returncode == 0
    default = _json(built_default)
    assert default["written"] is True
    assert default["selected_export_count"] == 0
    assert _source_execution_count(sentinel) == 2

    inspected_for_selection = _run(
        project,
        "inspect",
        "--bundle",
        str(default_bundle),
        "--json",
        environment=environment,
    )
    assert inspected_for_selection.returncode == 0
    selection_inspection = _json(inspected_for_selection)
    locked_part = selection_inspection["dependency_lock"]["selected_parts"][0]
    part = locked_part["selected_part"]
    step_selector = (
        f"{part['library_namespace']}@{part['library_version']}:{part['part_key']}"
    )
    assert _source_execution_count(sentinel) == 2

    selected_bundle = tmp_path / "selected.volt"
    built_selected = _run(
        project,
        "build",
        "--project",
        str(project),
        "--output",
        str(selected_bundle),
        "--export",
        "board-svg",
        "--json",
        environment=environment,
    )
    assert built_selected.returncode == 0
    selected = _json(built_selected)
    assert selected["selected_export_count"] == 1
    assert selected["artifact_count"] == default["artifact_count"] + 1
    assert _source_execution_count(sentinel) == 3

    step_bundle = tmp_path / "step.volt"
    built_step = _run(
        project,
        "build",
        "--project",
        str(project),
        "--output",
        str(step_bundle),
        "--export",
        "step",
        "--part",
        step_selector,
        "--json",
        environment=environment,
    )
    assert built_step.returncode == 0
    step = _json(built_step)
    assert step["selected_export_count"] == 1
    assert step["artifact_count"] == default["artifact_count"] + 1
    assert _source_execution_count(sentinel) == 4

    project.joinpath("project_entry.py").write_text(
        "raise RuntimeError('bundle operations executed project source')\n",
        encoding="utf-8",
    )

    inspected_default = _run(
        project,
        "inspect",
        "--bundle",
        str(default_bundle),
        "--json",
        environment=environment,
    )
    assert inspected_default.returncode == 0
    default_inspection = _json(inspected_default)
    assert default_inspection["integrity"] == "verified-v2"
    assert default_inspection["project"]["name"] == "verified-cli-single"
    assert [item["design"] for item in default_inspection["logical"]] == ["controller"]
    assert [item["name"] for item in default_inspection["schematics"]] == ["Main"]
    assert [item["name"] for item in default_inspection["boards"]] == ["Main"]
    assert len(default_inspection["compiled_boards"]) == 1
    assert len(default_inspection["scenes"]) == 1
    assert len(default_inspection["parts"]) == 1
    assert default_inspection["selected_exports"] == []
    assert {item["kind"] for item in default_inspection["artifacts"]} >= {
        "logical_model",
        "schematic_model",
        "board_model",
        "compiled_board",
        "diagnostics",
        "project_tests",
        "board_scene",
    }

    identical = _run(
        project,
        "diff",
        str(default_bundle),
        str(default_bundle),
        "--json",
        environment=environment,
    )
    assert identical.returncode == 0
    assert _json(identical)["status"] == "identical"

    different = _run(
        project,
        "diff",
        str(default_bundle),
        str(selected_bundle),
        "--json",
        environment=environment,
    )
    assert different.returncode == 1
    difference = _json(different)
    assert difference["status"] == "different"
    assert [artifact["kind"] for artifact in difference["added"]] == ["board_svg"]
    assert difference["removed"] == []
    assert difference["changed"] == []

    empty_output = tmp_path / "empty-export"
    exported_empty = _run(
        project,
        "export",
        "--bundle",
        str(default_bundle),
        "--output",
        str(empty_output),
        "--json",
        environment=environment,
    )
    assert exported_empty.returncode == 0
    assert _json(exported_empty)["artifacts"] == []
    assert list(empty_output.iterdir()) == []

    selected_output = tmp_path / "selected-export"
    exported_selected = _run(
        project,
        "export",
        "--bundle",
        str(selected_bundle),
        "--output",
        str(selected_output),
        "--json",
        environment=environment,
    )
    assert exported_selected.returncode == 0
    [copied] = _json(exported_selected)["artifacts"]
    assert copied["kind"] == "board_svg"
    assert Path(copied["output"]).read_bytes().startswith(b"<svg")

    step_output = tmp_path / "step-export"
    exported_step = _run(
        project,
        "export",
        "--bundle",
        str(step_bundle),
        "--output",
        str(step_output),
        "--kind",
        "step-asset",
        "--json",
        environment=environment,
    )
    assert exported_step.returncode == 0
    [copied_step] = _json(exported_step)["artifacts"]
    assert copied_step["kind"] == "step_asset"
    assert Path(copied_step["output"]).read_text(encoding="utf-8").startswith(
        "ISO-10303-21;"
    )
    assert _source_execution_count(sentinel) == 4


def test_check_human_output_is_deterministic(tmp_path):
    project = _copy_fixture(tmp_path, "single_board")

    completed = _run(project, "check", "--project", str(project))

    assert completed.returncode == 0
    assert completed.stderr == ""
    assert (
        completed.stdout
        == "Check: clean (0 errors, 0 warnings, 0 infos; 0 failed tests)\n"
    )


def test_fresh_scaffold_completes_the_documented_verified_path(tmp_path):
    initialized = _run(tmp_path, "init", "controller")
    project = tmp_path / "controller"
    assert initialized.returncode == 0
    assert initialized.stderr == ""
    assert initialized.stdout == f"Initialized Volt project at {project}\n"

    checked = _run(project, "check", "--json")
    assert checked.returncode == 0
    assert _json(checked)["ok"] is True

    bundle = project / "build" / "controller.volt"
    assert not bundle.parent.exists()
    built = _run(
        project,
        "build",
        "--output",
        str(bundle),
        "--json",
    )
    assert built.returncode == 0
    assert _json(built)["selected_export_count"] == 0

    inspected = _run(project, "inspect", "--bundle", str(bundle), "--json")
    assert inspected.returncode == 0
    assert _json(inspected)["project"]["name"] == "controller"

    diffed = _run(project, "diff", str(bundle), str(bundle), "--json")
    assert diffed.returncode == 0
    assert _json(diffed)["status"] == "identical"

    output = tmp_path / "controller-export"
    exported = _run(
        project,
        "export",
        "--bundle",
        str(bundle),
        "--output",
        str(output),
        "--json",
    )
    assert exported.returncode == 0
    assert _json(exported)["artifacts"] == []
    assert list(output.iterdir()) == []


def test_project_local_volt_module_cannot_shadow_the_isolated_worker(tmp_path):
    project = _copy_fixture(tmp_path, "single_board")
    project.joinpath("volt.py").write_text(
        "raise RuntimeError('project-local volt module shadowed the worker')\n",
        encoding="utf-8",
    )

    completed = _run(
        tmp_path,
        "check",
        "--project",
        str(project),
        "--json",
    )

    assert completed.returncode == 0
    assert _json(completed)["ok"] is True


def test_entrypoint_cannot_replace_an_already_loaded_package(tmp_path):
    project = _copy_fixture(tmp_path, "single_board")
    project.joinpath("volt.toml").write_text(
        '[project]\nentrypoint = "volt:main"\n',
        encoding="utf-8",
    )
    project.joinpath("volt.py").write_text(
        "def main():\n    raise AssertionError('must not run')\n",
        encoding="utf-8",
    )

    completed = _run(
        tmp_path,
        "check",
        "--project",
        str(project),
        "--json",
    )

    assert completed.returncode == 2
    error = _json(completed)["error"]
    assert error["code"] == "project-entrypoint-module-conflict"
    assert "already-loaded module outside the project root" in error["message"]


def test_zero_board_selection_reports_that_the_project_has_no_boards():
    with pytest.raises(CliError, match="Project has no Boards") as caught:
        _board(SimpleNamespace(boards=()), None)

    assert caught.value.code == "invalid-board-selector"


def test_multiple_named_boards_require_exact_export_and_inspection_selectors(
    tmp_path,
):
    project = _copy_fixture(tmp_path, "multiple_boards")

    ambiguous = _run(
        project,
        "build",
        "--project",
        str(project),
        "--output",
        str(tmp_path / "ambiguous.volt"),
        "--export",
        "board-svg",
        "--json",
    )
    assert ambiguous.returncode == 2
    error = _json(ambiguous)
    assert error["error"]["code"] == "exact-board-selector-required"
    assert "controller:Main" in error["error"]["message"]
    assert "controller:Compact" in error["error"]["message"]
    assert "controller:Extended" in error["error"]["message"]

    bundle = tmp_path / "alpha.volt"
    selected = _run(
        project,
        "build",
        "--project",
        str(project),
        "--output",
        str(bundle),
        "--export",
        "board-svg",
        "--board",
        "controller:Main",
        "--json",
    )
    assert selected.returncode == 0
    assert _json(selected)["selected_export_count"] == 1

    short_selector = _run(
        project,
        "inspect",
        "--bundle",
        str(bundle),
        "--board",
        "Main",
        "--json",
    )
    assert short_selector.returncode == 2
    assert _json(short_selector)["error"]["code"] == "exact-board-selector-required"

    exact_selector = _run(
        project,
        "inspect",
        "--bundle",
        str(bundle),
        "--board",
        "controller:Main",
        "--json",
    )
    assert exact_selector.returncode == 0
    inspection = _json(exact_selector)
    assert [(item["design"], item["name"]) for item in inspection["boards"]] == [
        ("controller", "Main")
    ]
    assert [item["identity"]["board"] for item in inspection["compiled_boards"]] == [
        "Main"
    ]
    assert len(inspection["scenes"]) == 1


def test_supported_typed_export_lowerings_add_only_the_selected_artifact(tmp_path):
    project = _copy_fixture(tmp_path, "single_board")
    baseline_path = tmp_path / "baseline.volt"
    baseline_result = _run(
        project,
        "build",
        "--project",
        str(project),
        "--output",
        str(baseline_path),
        "--json",
    )
    assert baseline_result.returncode == 0
    baseline = _json(baseline_result)

    selections = (
        ("schematic-svg", (), "schematic_svg"),
        ("bom", (), "bom"),
        ("cpl", (), "cpl"),
        ("board-layer-image", ("--layer", "F.Cu"), "board_layer_image"),
    )
    for export_kind, selector_arguments, artifact_kind in selections:
        output = tmp_path / f"{export_kind}.volt"
        completed = _run(
            project,
            "build",
            "--project",
            str(project),
            "--output",
            str(output),
            "--export",
            export_kind,
            *selector_arguments,
            "--json",
        )
        assert completed.returncode == 0
        result = _json(completed)
        assert result["artifact_count"] == baseline["artifact_count"] + 1
        assert result["selected_export_count"] == 1

        inspected = _run(
            project,
            "inspect",
            "--bundle",
            str(output),
            "--json",
        )
        assert inspected.returncode == 0
        assert [
            artifact["kind"] for artifact in _json(inspected)["selected_exports"]
        ] == [artifact_kind]


def test_failed_tests_are_persisted_without_reclassifying_structural_or_target_state(
    tmp_path,
):
    project = tmp_path / "failed-test"
    project.mkdir()
    project.joinpath("volt.toml").write_text(
        '[project]\nentrypoint = "project_entry:main"\n',
        encoding="utf-8",
    )
    project.joinpath("project_entry.py").write_text(
        """import volt

def main():
    project = volt.Project("failed-test")

    @project.design
    def design():
        return volt.Design("controller")

    @project.design.test
    def power_reaches_connector(check):
        check.net("POWER").connects("J1.1")

    return project
""",
        encoding="utf-8",
    )
    bundle = tmp_path / "failed.volt"

    completed = _run(
        project,
        "build",
        "--project",
        str(project),
        "--output",
        str(bundle),
        "--json",
    )

    assert completed.returncode == 1
    result = _json(completed)
    assert result["ok"] is False
    assert result["status"] == "failed"
    assert result["structurally_valid"] is True
    assert result["target_ready"] is True
    assert result["diagnostics"]["summary"] == {
        "errors": 0,
        "infos": 0,
        "warnings": 0,
    }
    assert result["tests"]["summary"] == {"failed": 1, "passed": 0}
    [failed] = result["tests"]["tests"]
    assert failed["stage"] == "design"
    assert failed["name"] == "power_reaches_connector"
    assert failed["ok"] is False
    assert "POWER" in failed["message"]
    assert result["written"] is True
    assert bundle.joinpath("manifest.volt.json").is_file()

    human = _run(project, "check", "--project", str(project))
    assert human.returncode == 1
    assert human.stderr == ""
    assert "1 failed tests" in human.stdout
    assert "design.power_reaches_connector:" in human.stdout
    assert "POWER" in human.stdout

    diagnostics = _run(
        project,
        "diagnostics",
        "--project",
        str(project),
        "--check",
        "--json",
    )
    assert diagnostics.returncode == 1
    diagnostic_payload = _json(diagnostics)
    assert diagnostic_payload["tests"] == result["tests"]

    inspected = _run(
        project,
        "inspect",
        "--bundle",
        str(bundle),
        "--json",
    )
    assert inspected.returncode == 0
    inspection = _json(inspected)
    assert inspection["status"] == "failed"
    assert inspection["tests"]["summary"] == {"failed": 1, "passed": 0}
    assert inspection["tests"]["tests"] == result["tests"]["tests"]


def test_typed_selected_export_and_bundle_failures_use_machine_error_envelope(
    tmp_path,
):
    project = _copy_fixture(tmp_path, "single_board")

    unsupported = _run(
        project,
        "build",
        "--project",
        str(project),
        "--output",
        str(tmp_path / "unsupported.volt"),
        "--export",
        "whole-board-glb",
        "--json",
    )
    assert unsupported.returncode == 2
    error = _json(unsupported)
    assert error["format"] == "volt.cli-result"
    assert error["schema_version"] == 1
    assert error["command"] == "build"
    assert error["error"]["code"] == "selected-export-failed"
    assert "unsupported by this producer contract" in error["error"]["message"]

    missing = _run(
        project,
        "inspect",
        "--bundle",
        str(tmp_path / "missing.volt"),
        "--json",
    )
    assert missing.returncode == 2
    assert _json(missing)["error"]["code"] == "bundle-not-found"


def test_export_cleans_staging_after_a_non_oserror_copy_failure(
    tmp_path,
    monkeypatch,
):
    project = _copy_fixture(tmp_path, "single_board")
    bundle = tmp_path / "selected.volt"
    built = _run(
        project,
        "build",
        "--project",
        str(project),
        "--output",
        str(bundle),
        "--export",
        "board-svg",
        "--json",
    )
    assert built.returncode == 0

    output = tmp_path / "selected-exports"
    stage = tmp_path / ".selected-exports.volt-stage"

    def fail_copy(_path, _data):
        raise RuntimeError("injected non-OSError copy failure")

    monkeypatch.setattr(Path, "write_bytes", fail_copy)
    with pytest.raises(RuntimeError, match="injected non-OSError copy failure"):
        _handle_export(
            SimpleNamespace(
                bundle=bundle,
                output=output,
                export_filter=(),
                emit_json=False,
            )
        )

    assert not stage.exists()
    assert not output.exists()
