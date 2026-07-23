import json
import sys
from pathlib import Path

import pytest

from volt.cli import CliError, discover_project, load_project_config, main


def _write_project(
    root: Path,
    entrypoint: str = "project_entry:main",
    paths: str = "",
    extra_config: str = "",
):
    root.mkdir(parents=True, exist_ok=True)
    root.joinpath("volt.toml").write_text(
        f"""[project]
entrypoint = "{entrypoint}"

[paths]
{paths}
{extra_config}
""",
        encoding="utf-8",
    )


def _write_entrypoint(root: Path, body: str):
    root.joinpath("project_entry.py").write_text(body, encoding="utf-8")
    sys.modules.pop("project_entry", None)


def _write_library(root: Path):
    root.mkdir(parents=True, exist_ok=True)
    root.joinpath("volt.toml").write_text(
        """[library]
entrypoint = "library_entry:LIB"
""",
        encoding="utf-8",
    )
    root.joinpath("resistor.glb").write_bytes(b"cli-library-model")
    root.joinpath("library_entry.py").write_text(
        f'''from pathlib import Path

import volt

LIB = volt.Library("test.cli.library", version="1.0.0")
LIB.part(
    "R-1K",
    pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
    symbol=volt.SchematicSymbolSpec(
        "test.cli:R-1K",
        pins=(
            volt.SchematicSymbolSpec.pin("1", 1, (-5, 0)),
            volt.SchematicSymbolSpec.pin("2", 2, (5, 0)),
        ),
        primitives=(volt.SchematicSymbolSpec.line((-3, 0), (3, 0)),),
    ),
    manufacturer="Test",
    mpn="R-1K",
    package="0603",
    footprint=volt.Footprint(
        ("Test", "R-1K"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-1, 0), size=(1, 1)),
            volt.FootprintPad.surface_mount("2", at=(1, 0), size=(1, 1)),
        ),
    ),
    pads={{1: "1", 2: "2"}},
    model_3d=volt.PartModel3D(Path(__file__).with_name("resistor.glb")),
)
''',
        encoding="utf-8",
    )
    sys.modules.pop("library_entry", None)


def _read_model_json(capsys):
    captured = capsys.readouterr()
    assert captured.err == ""
    return json.loads(captured.out)


def _read_stdout_json(capsys):
    captured = capsys.readouterr()
    assert captured.err == ""
    return json.loads(captured.out)


def _write_bad_led_entrypoint(root: Path, *, expect_diagnostics: bool = False):
    expectations = (
        """
    project.expect_diagnostic(code="UNCONNECTED_REQUIRED_PIN")
    project.expect_diagnostic(code="SINGLE_PIN_NET")
"""
        if expect_diagnostics
        else ""
    )
    _write_entrypoint(
        root,
        f"""import volt

def main():
    project = volt.Project("bad-led")
{expectations}
    @project.design
    def design():
        design = volt.Design("bad-led")
        lonely = design.net("LONELY")
        resistor = design.R("1k", ref="R1")
        resistor.dnp(True)
        lonely += resistor[1]
        return design

    return project.run()
""",
    )


def test_main_help_uses_argparse(capsys):
    with pytest.raises(SystemExit) as exc:
        main(["--help"])

    assert exc.value.code == 0
    assert "usage: volt" in capsys.readouterr().out


def test_run_subcommand_help_uses_argparse(capsys):
    with pytest.raises(SystemExit) as exc:
        main(["run", "--help"])

    assert exc.value.code == 0
    output = capsys.readouterr().out
    assert "usage: volt run" in output
    assert "--project" in output


def test_discovers_volt_toml_by_walking_up_from_cwd(tmp_path, monkeypatch):
    root = tmp_path / "board"
    nested = root / "hardware" / "pcb"
    nested.mkdir(parents=True)
    _write_project(root)
    monkeypatch.chdir(nested)

    config = discover_project()

    assert config.root == root
    assert config.config_path == root / "volt.toml"
    assert config.entrypoint == "project_entry:main"


def test_project_override_accepts_root_directory_outside_cwd(tmp_path, monkeypatch):
    root = tmp_path / "selected"
    _write_project(root)
    monkeypatch.chdir(tmp_path)

    config = discover_project(project=root)

    assert config.root == root
    assert config.config_path == root / "volt.toml"


def test_project_override_accepts_config_file(tmp_path, monkeypatch):
    root = tmp_path / "selected"
    _write_project(root)
    monkeypatch.chdir(tmp_path)

    config = discover_project(project=root / "volt.toml")

    assert config.root == root
    assert config.config_path == root / "volt.toml"


def test_project_config_resolves_relative_paths_from_project_root(tmp_path):
    root = tmp_path / "board"
    absolute = tmp_path / "cache"
    _write_project(
        root,
        paths=f"""artifacts = "artifacts"
cache = "{absolute.as_posix()}"
""",
    )

    config = load_project_config(root / "volt.toml")

    assert config.paths["artifacts"] == root / "artifacts"
    assert config.paths["cache"] == absolute


def test_run_loads_entrypoint_from_project_root_and_sets_cwd(tmp_path, monkeypatch):
    root = tmp_path / "board"
    nested = root / "nested"
    nested.mkdir(parents=True)
    _write_project(root)
    _write_entrypoint(
        root,
        """from pathlib import Path

def main():
    Path("called.txt").write_text(str(Path.cwd()), encoding="utf-8")
""",
    )
    monkeypatch.chdir(nested)

    assert main(["run"]) == 0

    assert (root / "called.txt").read_text(encoding="utf-8") == str(root)
    assert Path.cwd() == nested


def test_run_project_override_sets_cwd_to_override_root(tmp_path, monkeypatch):
    root = tmp_path / "selected"
    _write_project(root)
    _write_entrypoint(
        root,
        """from pathlib import Path

def main():
    Path("override-cwd.txt").write_text(str(Path.cwd()), encoding="utf-8")
""",
    )
    monkeypatch.chdir(tmp_path)

    assert main(["run", "--project", str(root)]) == 0

    assert (root / "override-cwd.txt").read_text(encoding="utf-8") == str(root)


def test_run_reloads_entrypoint_module_for_each_project(tmp_path, monkeypatch):
    first = tmp_path / "first"
    second = tmp_path / "second"
    _write_project(first)
    _write_project(second)
    _write_entrypoint(
        first,
        """from pathlib import Path

VALUE = "first"

def main():
    Path("called.txt").write_text(VALUE, encoding="utf-8")
""",
    )
    _write_entrypoint(
        second,
        """from pathlib import Path

VALUE = "second"

def main():
    Path("called.txt").write_text(VALUE, encoding="utf-8")
""",
    )
    monkeypatch.chdir(tmp_path)

    assert main(["run", "--project", str(first)]) == 0
    assert main(["run", "--project", str(second)]) == 0

    assert (first / "called.txt").read_text(encoding="utf-8") == "first"
    assert (second / "called.txt").read_text(encoding="utf-8") == "second"


def test_run_restores_sys_path_when_entrypoint_mutates_it(tmp_path, monkeypatch):
    root = tmp_path / "board"
    _write_project(root)
    _write_entrypoint(
        root,
        """import sys

def main():
    sys.path.clear()
""",
    )
    monkeypatch.chdir(tmp_path)
    previous_sys_path = list(sys.path)

    assert main(["run", "--project", str(root)]) == 0

    assert sys.path == previous_sys_path


def test_loader_rejects_entrypoint_without_module_and_function(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root, entrypoint="project_entry")

    assert main(["run", "--project", str(root)]) == 2

    assert "module:function" in capsys.readouterr().err


def test_loader_reports_missing_module_with_actionable_exit_code(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root, entrypoint="missing_module:main")

    assert main(["run", "--project", str(root)]) == 2

    output = capsys.readouterr().err
    assert "missing_module" in output
    assert "project root" in output


def test_loader_reports_missing_function_with_actionable_exit_code(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root, entrypoint="project_entry:missing")
    _write_entrypoint(root, "def main():\n    return None\n")

    assert main(["run", "--project", str(root)]) == 2

    output = capsys.readouterr().err
    assert "project_entry:missing" in output
    assert "has no attribute" in output


def test_cli_error_maps_to_declared_exit_code(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_entrypoint(
        root,
        """from volt.cli import CliError

def main():
    raise CliError("gate failed", exit_code=1)
""",
    )

    assert main(["run", "--project", str(root)]) == 1

    assert "gate failed" in capsys.readouterr().err


def test_model_json_emits_clean_result_to_stdout(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

CALLS = 0

def main():
    global CALLS
    CALLS += 1
    if CALLS > 1:
        raise RuntimeError("entrypoint called more than once")
    project = volt.Project("status-led")

    @project.design
    def design():
        return volt.Design("status-led")

    return project.run()
""",
    )

    assert main(["model", "--json", "--project", str(root)]) == 0

    payload = _read_model_json(capsys)
    assert payload["status"] == "clean"
    assert payload["diagnostics"]["status"] == "clean"
    assert payload["diagnostics"]["summary"] == {
        "errors": 0,
        "infos": 0,
        "warnings": 0,
    }
    assert [item["name"] for item in payload["models"]["designs"]] == ["status-led"]
    assert payload["models"]["schematics"] == []
    assert payload["models"]["boards"] == []
    assert payload["models"]["designs"][0]["model"]["components"] == []


def test_model_json_accepts_project_entrypoint_from_project_root(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    root.joinpath("project_name.txt").write_text("status-led", encoding="utf-8")
    _write_entrypoint(
        root,
        """from pathlib import Path

import volt

def main():
    project = volt.Project("status-led")

    @project.design
    def design():
        return volt.Design(Path("project_name.txt").read_text(encoding="utf-8").strip())

    return project
""",
    )

    assert main(["model", "--json", "--project", str(root)]) == 0

    payload = _read_model_json(capsys)
    assert payload["status"] == "clean"
    assert [item["name"] for item in payload["models"]["designs"]] == ["status-led"]


def test_model_json_keeps_project_stdout_out_of_json_stream(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def main():
    print("project chatter")
    project = volt.Project("status-led")

    @project.design
    def design():
        return volt.Design("status-led")

    return project.run()
""",
    )

    assert main(["model", "--json", "--project", str(root)]) == 0

    captured = capsys.readouterr()
    payload = json.loads(captured.out)
    assert captured.err == "project chatter\n"
    assert payload["status"] == "clean"
    assert [item["name"] for item in payload["models"]["designs"]] == ["status-led"]


def test_model_json_keeps_expected_diagnostics_non_gating(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def main():
    project = volt.Project("bad-led")
    project.expect_diagnostic(code="UNCONNECTED_REQUIRED_PIN")
    project.expect_diagnostic(code="SINGLE_PIN_NET")

    @project.design
    def design():
        design = volt.Design("bad-led")
        lonely = design.net("LONELY")
        resistor = design.R("1k", ref="R1")
        resistor.dnp(True)
        lonely += resistor[1]
        return design

    return project.run()
""",
    )

    assert main(["model", "--json", "--project", str(root)]) == 0

    payload = _read_model_json(capsys)
    assert payload["status"] == "expected-diagnostics"
    assert payload["diagnostics"]["status"] == "expected-diagnostics"
    assert payload["diagnostics"]["summary"]["errors"] > 0
    assert [item["code"] for item in payload["diagnostics"]["unexpected"]] == []


def test_model_json_keeps_failed_status_non_gating(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def main():
    project = volt.Project("bad-led")

    @project.design
    def design():
        design = volt.Design("bad-led")
        design.R("1k", ref="R1")
        return design

    return project.run()
""",
    )

    assert main(["model", "--json", "--project", str(root)]) == 0

    payload = _read_model_json(capsys)
    assert payload["status"] == "failed"
    assert payload["diagnostics"]["status"] == "failed"
    assert payload["diagnostics"]["summary"]["errors"] > 0
    assert "UNCONNECTED_REQUIRED_PIN" in {
        item["code"] for item in payload["diagnostics"]["unexpected"]
    }


def test_diagnostics_human_output_groups_by_report_and_source(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_bad_led_entrypoint(root)

    assert main(["diagnostics", "--project", str(root)]) == 0

    captured = capsys.readouterr()
    assert captured.err == ""
    assert "Diagnostics: failed" in captured.out
    assert "1 error" in captured.out
    assert "1 warning" in captured.out
    assert "logical.default" in captured.out
    assert "logical:bad-led" in captured.out
    assert "ERROR UNCONNECTED_REQUIRED_PIN [design]" in captured.out
    assert "WARNING SINGLE_PIN_NET [design]" in captured.out
    assert "\x1b[" not in captured.out
    assert captured.out.index("UNCONNECTED_REQUIRED_PIN") < captured.out.index(
        "SINGLE_PIN_NET"
    )


def test_diagnostics_json_reuses_bundle_schema_and_filters(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_bad_led_entrypoint(root)

    assert (
        main(
            [
                "diagnostics",
                "--json",
                "--stage",
                "design",
                "--severity",
                "warning",
                "--project",
                str(root),
            ]
        )
        == 0
    )

    payload = _read_stdout_json(capsys)
    assert set(payload) == {
        "diagnostics",
        "expected",
        "missing_expected",
        "status",
        "summary",
        "unexpected",
    }
    assert payload["status"] == "failed"
    assert payload["summary"] == {"errors": 0, "infos": 0, "warnings": 1}
    assert [item["code"] for item in payload["diagnostics"]] == ["SINGLE_PIN_NET"]
    diagnostic = payload["diagnostics"][0]
    assert {
        "board",
        "category",
        "code",
        "design",
        "entities",
        "expect_diagnostic_kwargs",
        "measurement",
        "message",
        "overlays",
        "report",
        "rule",
        "severity",
        "source",
        "stage",
    } <= set(diagnostic)
    assert diagnostic["stage"] == "design"
    assert diagnostic["source"] == "logical:bad-led"
    assert diagnostic["report"] == "logical.default"
    assert diagnostic["severity"] == "warning"
    assert [item["code"] for item in payload["unexpected"]] == ["SINGLE_PIN_NET"]


def test_diagnostics_check_mode_uses_project_ok(tmp_path, capsys):
    failed = tmp_path / "failed"
    _write_project(failed)
    _write_bad_led_entrypoint(failed)

    assert main(["diagnostics", "--project", str(failed)]) == 0
    capsys.readouterr()
    assert main(["diagnostics", "--check", "--project", str(failed)]) == 1
    capsys.readouterr()

    expected = tmp_path / "expected"
    _write_project(expected)
    _write_bad_led_entrypoint(expected, expect_diagnostics=True)

    assert main(["diagnostics", "--check", "--project", str(expected)]) == 0


def test_diagnostics_invalid_filters_exit_with_actionable_error(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_bad_led_entrypoint(root)

    assert (
        main(["diagnostics", "--severity", "critical", "--project", str(root)])
        == 2
    )
    assert "Invalid diagnostic severity 'critical'" in capsys.readouterr().err

    assert main(["diagnostics", "--stage", "pcb", "--project", str(root)]) == 2
    assert "Invalid diagnostic stage 'pcb'" in capsys.readouterr().err


def test_build_writes_bundle_when_project_ok(tmp_path, capsys):
    root = tmp_path / "board"
    output = tmp_path / "bundle"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def main():
    project = volt.Project("status-led")

    @project.design
    def design():
        return volt.Design("status-led")

    return project.run()
""",
    )

    assert main(["build", "--output", str(output), "--project", str(root)]) == 0

    captured = capsys.readouterr()
    assert captured.err == ""
    assert str(output) in captured.out
    manifest = json.loads((output / "manifest.volt.json").read_text(encoding="utf-8"))
    assert manifest["status"] == "clean"
    assert manifest["profile"] == "default"
    assert output.joinpath("logical", "status-led.volt.json").is_file()


def test_build_json_gates_failed_project_without_writing(tmp_path, capsys):
    root = tmp_path / "board"
    output = tmp_path / "bundle"
    _write_project(root)
    _write_bad_led_entrypoint(root)

    assert (
        main(["build", "--json", "--output", str(output), "--project", str(root)])
        == 1
    )

    payload = _read_stdout_json(capsys)
    assert payload["status"] == "failed"
    assert payload["ok"] is False
    assert payload["written"] is False
    assert payload["output"] == str(output)
    assert payload["diagnostics"]["summary"]["errors"] > 0
    assert not output.exists()


def test_build_flat_uses_existing_flat_artifact_writer(tmp_path, capsys):
    root = tmp_path / "board"
    output = tmp_path / "flat"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def main():
    project = volt.Project("status-led")

    @project.design
    def design():
        return volt.Design("status-led")

    return project
""",
    )

    assert (
        main(["build", "--flat", "--output", str(output), "--project", str(root)])
        == 0
    )

    capsys.readouterr()
    assert output.joinpath("status-led.volt.json").is_file()
    assert output.joinpath("status-led.validation.json").is_file()
    assert not output.joinpath("manifest.volt.json").exists()


def test_info_json_reports_project_metadata_without_gating(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(
        root,
        extra_config="""
[manufacturing]
profile = "profiles/jlcpcb.volt.json"
""",
    )
    _write_bad_led_entrypoint(root)

    assert main(["info", "--json", "--project", str(root)]) == 0

    payload = _read_stdout_json(capsys)
    assert payload["project"] == {
        "name": "bad-led",
        "version": None,
        "description": None,
    }
    assert payload["status"] == "failed"
    assert payload["ok"] is False
    assert payload["stages"] == [
        {"name": "design", "model_count": 1, "tests": {"total": 0, "failed": 0}}
    ]
    assert payload["models"] == {"designs": 1, "schematics": 0, "boards": 0}
    assert payload["diagnostics"]["summary"]["errors"] > 0
    assert payload["tests"] == {"total": 0, "failed": 0}
    assert payload["manufacturing_profile"] == {
        "path": "profiles/jlcpcb.volt.json",
        "resolved_path": str(root / "profiles" / "jlcpcb.volt.json"),
    }


def test_init_scaffolds_runnable_project_and_refuses_overwrite(
    tmp_path, monkeypatch, capsys
):
    monkeypatch.chdir(tmp_path)

    assert main(["init", "blink", "--profile", "jlcpcb"]) == 0

    root = tmp_path / "blink"
    config_text = root.joinpath("volt.toml").read_text(encoding="utf-8")
    entrypoint_text = root.joinpath("main.py").read_text(encoding="utf-8")
    assert 'profile = "profiles/jlcpcb.volt.json"' in config_text
    assert "CapabilityProfile.from_file" in entrypoint_text
    assert "set_capability_profile" in entrypoint_text
    assert root.joinpath("profiles", "jlcpcb.volt.json").is_file()
    capsys.readouterr()

    assert main(["model", "--json", "--project", str(root)]) == 0
    payload = _read_model_json(capsys)
    assert payload["status"] == "clean"
    [board] = payload["models"]["boards"]
    assert board["model"]["board"]["capability_profile"]["name"] == (
        "JLCPCB 2-layer FR-4 capability snapshot"
    )

    assert main(["init", "blink", "--profile", "jlcpcb"]) == 2
    refused = capsys.readouterr()
    assert refused.out == ""
    assert "Refusing to overwrite existing file" in refused.err


def test_model_json_emits_named_multi_model_arrays(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def _design(name):
    return volt.Design(name)

def _schematic(design):
    return design.schematic("Main")

def _board(design):
    board = design.add_board("Main")
    board.set_rectangular_outline(origin=(0, 0), size=(20, 10))
    return board

def main():
    project = volt.Project("control-panel")

    @project.design
    def design():
        return (_design("main:controller"), _design("front-panel"))

    @project.schematic
    def schematic(context):
        return tuple(_schematic(design) for design in context.designs)

    @project.board
    def board(context):
        return tuple(_board(design) for design in context.designs)

    return project.run()
""",
    )

    assert main(["model", "--json", "--project", str(root)]) == 0

    payload = _read_model_json(capsys)
    assert [item["name"] for item in payload["models"]["designs"]] == [
        "main:controller",
        "front-panel",
    ]
    assert [
        (item["name"], item["design"], item["schematic"])
        for item in payload["models"]["schematics"]
    ] == [
        ("main~1controller:Main", "main:controller", "Main"),
        ("front-panel:Main", "front-panel", "Main"),
    ]
    assert [
        (item["name"], item["design"], item["board"])
        for item in payload["models"]["boards"]
    ] == [
        ("main~1controller:Main", "main:controller", "Main"),
        ("front-panel:Main", "front-panel", "Main"),
    ]
    assert payload["models"]["schematics"][0]["model"]["sheets"][0]["name"] == "Main"
    assert payload["models"]["boards"][0]["model"]["board"]["name"] == "Main"


def test_model_json_selectors_filter_arrays(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def _design(name):
    return volt.Design(name)

def _schematic(design):
    return design.schematic("Main")

def _board(design):
    board = design.add_board("Main")
    board.set_rectangular_outline(origin=(0, 0), size=(20, 10))
    return board

def main():
    project = volt.Project("control-panel")

    @project.design
    def design():
        return (_design("main:controller"), _design("front-panel"))

    @project.schematic
    def schematic(context):
        return tuple(_schematic(design) for design in context.designs)

    @project.board
    def board(context):
        return tuple(_board(design) for design in context.designs)

    return project.run()
""",
    )

    assert main(["model", "--json", "--project", str(root), "--design", "front-panel"]) == 0
    payload = _read_model_json(capsys)
    assert [item["name"] for item in payload["models"]["designs"]] == ["front-panel"]
    assert [item["name"] for item in payload["models"]["schematics"]] == [
        "front-panel:Main"
    ]
    assert [item["name"] for item in payload["models"]["boards"]] == [
        "front-panel:Main"
    ]

    assert (
        main(
            [
                "model",
                "--json",
                "--project",
                str(root),
                "--schematic",
                "main~1controller:Main",
                "--board",
                "front-panel:Main",
            ]
        )
        == 0
    )
    payload = _read_model_json(capsys)
    assert [item["name"] for item in payload["models"]["designs"]] == [
        "main:controller",
        "front-panel",
    ]
    assert [item["name"] for item in payload["models"]["schematics"]] == [
        "main~1controller:Main"
    ]
    assert [item["name"] for item in payload["models"]["boards"]] == [
        "front-panel:Main"
    ]


def test_model_json_reports_ambiguous_and_missing_selectors(tmp_path, capsys):
    root = tmp_path / "board"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def _design(name):
    return volt.Design(name)

def _schematic(design):
    return design.schematic("Main")

def main():
    project = volt.Project("control-panel")

    @project.design
    def design():
        return (_design("main-controller"), _design("front-panel"))

    @project.schematic
    def schematic(context):
        return tuple(_schematic(design) for design in context.designs)

    return project.run()
""",
    )

    assert main(["model", "--json", "--project", str(root), "--schematic", "Main"]) == 2
    ambiguous = capsys.readouterr()
    assert ambiguous.out == ""
    assert "Ambiguous schematic selector 'Main'" in ambiguous.err
    assert "main-controller:Main" in ambiguous.err
    assert "front-panel:Main" in ambiguous.err

    assert main(["model", "--json", "--project", str(root), "--board", "missing"]) == 2
    missing = capsys.readouterr()
    assert missing.out == ""
    assert "No board named 'missing'" in missing.err
    assert "Candidates: <none>" in missing.err


def test_library_commands_build_reopen_inspect_and_extract_deterministically(tmp_path, capsys):
    source = tmp_path / "library"
    first = tmp_path / "first.voltlib"
    second = tmp_path / "second.voltlib"
    first_render = tmp_path / "first-render"
    second_render = tmp_path / "second-render"
    _write_library(source)

    assert main(["library", "check", str(source), "--json"]) == 0
    check = _read_stdout_json(capsys)
    assert check["format"] == "volt.library_result"
    assert check["schema_version"] == 1
    assert check["ok"] is True
    assert check["library_digest"].startswith("sha256:")

    assert main(["library", "build", str(source), "--output", str(first), "--json"]) == 0
    build = _read_stdout_json(capsys)
    assert build["written"] is True
    assert build["bundle_digest"].startswith("sha256:")
    assert main(["library", "build", str(source), "--output", str(second)]) == 0
    capsys.readouterr()
    assert first.read_bytes() == second.read_bytes()

    assert main(["library", "test", str(first), "--json"]) == 0
    tested = _read_stdout_json(capsys)
    assert tested["format"] == "volt.library-test-result"
    assert tested["parts"] == ["R-1K"]

    assert main(["library", "inspect", str(first), "R-1K", "--json"]) == 0
    inspection = _read_stdout_json(capsys)
    assert inspection["format"] == "volt.part-library-bundle"
    assert inspection["schema_version"] == 1
    assert inspection["part"]["exact_reference"]["part_key"] == "R-1K"
    assert build["library_digest"] == inspection["library_digest"]
    assert build["parts"][0]["exact_reference"] == inspection["part"]["exact_reference"]
    assert [entry["path"] for entry in inspection["entries"]] == sorted(
        entry["path"] for entry in inspection["entries"]
    )

    assert (
        main(
            [
                "library",
                "render",
                str(first),
                "R-1K",
                "--output",
                str(first_render),
                "--json",
            ]
        )
        == 0
    )
    rendered = _read_stdout_json(capsys)
    assert [asset["kind"] for asset in rendered["assets"]] == [
        "schematic",
        "footprint",
        "model_3d",
    ]
    assert first_render.joinpath("03-model_3d.glb").read_bytes() == b"cli-library-model"
    assert (
        main(
            [
                "library",
                "render",
                str(second),
                "R-1K",
                "--output",
                str(second_render),
            ]
        )
        == 0
    )
    capsys.readouterr()
    assert first_render.joinpath("render.volt.json").read_bytes() == second_render.joinpath(
        "render.volt.json"
    ).read_bytes()


def test_library_commands_run_standard_library_end_to_end(tmp_path, capsys):
    source = tmp_path / "standard-library"
    bundle = tmp_path / "standard.voltlib"
    source.mkdir()
    source.joinpath("volt.toml").write_text(
        """[library]
entrypoint = "volt.libraries.stm32_usb_buck:LIB"
""",
        encoding="utf-8",
    )

    assert main(["library", "check", str(source), "--json"]) == 0
    check = _read_stdout_json(capsys)
    assert check["ok"] is True
    assert check["parts"] == []

    assert main(["library", "build", str(source), "--output", str(bundle)]) == 0
    capsys.readouterr()
    assert main(["library", "test", str(bundle), "--json"]) == 0
    tested = _read_stdout_json(capsys)
    assert tested["parts"] == check["parts"]

    assert main(["library", "inspect", str(bundle), "--json"]) == 0
    inspection = _read_stdout_json(capsys)
    assert inspection["library"]["namespace"] == "volt.benchmarks.stm32_usb_buck"


def test_library_commands_report_missing_assets_and_corrupt_bundles(tmp_path, capsys):
    source = tmp_path / "library"
    bundle = tmp_path / "library.voltlib"
    _write_library(source)
    source.joinpath("resistor.glb").unlink()

    assert main(["library", "check", str(source)]) == 2
    assert "Native library validation failed" in capsys.readouterr().err

    source.joinpath("resistor.glb").write_bytes(b"cli-library-model")
    assert main(["library", "build", str(source), "--output", str(bundle)]) == 0
    capsys.readouterr()
    assert (
        main(
            [
                "library",
                "render",
                str(bundle),
                "MISSING",
                "--output",
                str(tmp_path / "missing"),
            ]
        )
        == 2
    )
    assert "Native PartLibraryBundle query assets" in capsys.readouterr().err

    bundle.write_bytes(b"not a PartLibraryBundle")
    assert main(["library", "test", str(bundle)]) == 2
    assert "Invalid PartLibraryBundle" in capsys.readouterr().err


def test_library_commands_return_validation_failures_without_building(tmp_path, capsys):
    source = tmp_path / "invalid-library"
    output = tmp_path / "invalid.voltlib"
    source.mkdir()
    source.joinpath("volt.toml").write_text(
        """[library]
entrypoint = "library_entry:LIB"
""",
        encoding="utf-8",
    )
    source.joinpath("library_entry.py").write_text(
        """import volt

LIB = volt.Library("test.cli.invalid")
LIB.add(volt.Part(name="Broken", pins=(), footprint=None, pads={}))
""",
        encoding="utf-8",
    )
    sys.modules.pop("library_entry", None)

    assert main(["library", "check", str(source), "--json"]) == 1
    check = _read_stdout_json(capsys)
    assert check["ok"] is False
    assert {item["code"] for item in check["diagnostics"]["diagnostics"]} == {
        "LIBRARY_PART_MISSING_FOOTPRINT",
        "LIBRARY_PART_MISSING_PINS",
    }

    assert (
        main(["library", "build", str(source), "--output", str(output), "--json"])
        == 1
    )
    build = _read_stdout_json(capsys)
    assert build["written"] is False
    assert not output.exists()


def test_library_build_preserves_existing_output_on_write_failure(tmp_path, capsys, monkeypatch):
    source = tmp_path / "library"
    output = tmp_path / "library.voltlib"
    _write_library(source)
    output.write_bytes(b"previous bundle")
    original_write_bytes = Path.write_bytes

    def fail_temporary_write(path: Path, data: bytes) -> int:
        if path == output.with_name(f".{output.name}.tmp"):
            raise OSError("disk full")
        return original_write_bytes(path, data)

    monkeypatch.setattr(Path, "write_bytes", fail_temporary_write)

    assert main(["library", "build", str(source), "--output", str(output)]) == 2
    assert "Failed to write PartLibraryBundle" in capsys.readouterr().err
    assert output.read_bytes() == b"previous bundle"
    assert not output.with_name(f".{output.name}.tmp").exists()
