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


def _write_manufacturing_profile(root: Path) -> None:
    profile = {
        "format": "volt.capability_profile",
        "version": 1,
        "profile": {
            "name": "Generic 2-layer manufacturing test profile",
            "provenance": {
                "source": "Volt CLI manufacturing export test fixture",
                "as_of": "2026-06-21",
            },
            "minimum_track_width_mm": 0.2,
            "minimum_via_drill_mm": 0.3,
            "minimum_via_annular_mm": 0.6,
            "supported_copper_layer_counts": [2],
            "board_thickness_range_mm": {"minimum_mm": 0.8, "maximum_mm": 2.0},
            "available_copper_weights_oz": [1.0],
            "drill_diameter_range_mm": {"minimum_mm": 0.3, "maximum_mm": 6.0},
            "minimum_clearances": [
                {"first": "track", "second": "track", "clearance_mm": 0.2},
                {"first": "track", "second": "pad", "clearance_mm": 0.2},
            ],
        },
    }
    profile_path = root / "profiles" / "generic.volt.json"
    profile_path.parent.mkdir(parents=True, exist_ok=True)
    profile_path.write_text(json.dumps(profile, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _write_manufacturing_entrypoint(root: Path, *, lossy: bool = False) -> None:
    lossy_feature = (
        """
        board.add(
            volt.Hole(
                center=(8.0, 24.0),
                diameter=2.4,
                role="mounting",
                label="FH1",
                finished_diameter=2.0,
            )
        )
"""
        if lossy
        else ""
    )
    _write_entrypoint(
        root,
        f"""from pathlib import Path

import volt


def _rect_0603(ref):
    return volt.FootprintDefinition(
        ref,
        pads=(
            volt.FootprintPad.surface_mount(
                "1",
                at=(-0.75, 0.0),
                size=(0.8, 0.95),
                shape="rectangle",
            ),
            volt.FootprintPad.surface_mount(
                "2",
                at=(0.75, 0.0),
                size=(0.8, 0.95),
                shape="rectangle",
            ),
        ),
    )


def _header_1x02():
    return volt.FootprintDefinition(
        ("connectors", "PinHeader_1x02_P2.54mm"),
        pads=(
            volt.FootprintPad.through_hole(
                "1",
                at=(0.0, -1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
            volt.FootprintPad.through_hole(
                "2",
                at=(0.0, 1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
        ),
    )


def _net(design, name):
    return next(net for net in design.nets() if net.name == name)


def main():
    profile = volt.CapabilityProfile.from_file(Path("profiles/generic.volt.json"))
    project = volt.Project("status-led")

    @project.design
    def design():
        design = volt.Design("status-led")
        vcc = design.net("VCC", kind="power")
        led_a = design.net("LED_A")
        gnd = design.net("GND", kind="ground")
        j1 = design.connector_1x02(ref="J1")
        r1 = design.R("330", ref="R1")
        d1 = design.LED(ref="D1")
        vcc += j1[1], r1[1]
        led_a += r1[2], d1["A"]
        gnd += d1["K"], j1[2]
        j1.select_part(
            manufacturer="Generic",
            part_number="HDR-1x02",
            package="2.54mm-1x02",
            footprint=volt.FootprintDefinition(
                ("test", "Header1x02"),
                pads=_header_1x02().pads,
            ),
            pin_pads={{1: "1", 2: "2"}},
        )
        r1.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=_rect_0603(("test", "RectR0603")),
            pin_pads={{1: "1", 2: "2"}},
        )
        d1.select_part(
            manufacturer="Lite-On",
            part_number="LTST-C190KRKT",
            package="0603",
            footprint=_rect_0603(("test", "RectD0603")),
            pin_pads={{"A": "1", "K": "2"}},
        )
        for component in (j1, r1, d1):
            component.dnp(False)
        return design

    @project.board
    def board(context):
        [design] = context.designs
        board = design.board("Control")
        board.set_capability_profile(profile)
        front = board.add_layer("F.Cu", role="copper", side="top")
        back = board.add_layer("B.Cu", role="copper", side="bottom")
        silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
        board.set_layer_stack((front, back), thickness=1.6)
        board.set_design_rules(
            copper_clearance=0.25,
            min_track_width=0.25,
            min_via_drill=0.35,
            min_via_annular=0.7,
        )
        board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
        board.add(volt.Hole(center=(3.0, 3.0), diameter=3.2, role="mounting", label="MH1"))
        board.place(design.component("J1"), at=(6.0, 15.0), locked=True)
        board.place(design.component("R1"), at=(18.0, 15.0))
        board.place(design.component("D1"), at=(28.0, 15.0), rotation=180.0)
        vcc = _net(design, "VCC")
        led_a = _net(design, "LED_A")
        gnd = _net(design, "GND")
        board.add_track(
            vcc,
            layer=front,
            points=((6.0, 13.73), (12.0, 12.0), (17.25, 15.0)),
            width=0.25,
        )
        board.add_track(
            led_a,
            layer=front,
            points=((18.75, 15.0), (23.0, 12.0), (28.75, 12.0), (28.75, 15.0)),
            width=0.25,
        )
        board.add_track(
            gnd,
            layer=back,
            points=((6.0, 16.27), (23.0, 20.0)),
            width=0.25,
        )
        board.add_track(
            gnd,
            layer=front,
            points=((23.0, 20.0), (27.25, 15.0)),
            width=0.25,
        )
        board.add_via(
            gnd,
            at=(23.0, 20.0),
            start_layer=front,
            end_layer=back,
            drill=0.35,
            annular=0.75,
        )
        board.add_text("REV A", at=(4.0, 27.0), layer=silk, size=1.0)
{lossy_feature}
        return board

    return project.run()
""",
    )


def _directory_bytes(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


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


def test_export_manufacturing_writes_deterministic_native_package(tmp_path, capsys):
    root = tmp_path / "board"
    output = tmp_path / "manufacturing-package"
    _write_project(
        root,
        extra_config="""
[manufacturing]
profile = "profiles/generic.volt.json"
""",
    )
    _write_manufacturing_profile(root)
    _write_manufacturing_entrypoint(root)

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--json",
                "--output",
                str(output),
                "--project",
                str(root),
            ]
        )
        == 0
    )

    payload = _read_stdout_json(capsys)
    assert payload["status"] == "clean"
    assert payload["written"] is True
    assert payload["output"] == str(output)
    assert payload["board"] == {
        "design": "status-led",
        "name": "Control",
        "output_name": "Control",
    }

    manifest_path = output / "manufacturing" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert manifest["format"] == "volt.manufacturing_package"
    assert manifest["schema_version"] == 1
    assert manifest["project"] == {
        "name": "status-led",
        "version": None,
        "description": None,
    }
    assert manifest["command"] == {
        "name": "volt export manufacturing",
        "board": None,
        "archive": False,
    }
    assert manifest["profile"]["config"] == {
        "path": "profiles/generic.volt.json",
        "resolved_path": str(root / "profiles" / "generic.volt.json"),
    }
    assert manifest["profile"]["board"]["name"] == "Generic 2-layer manufacturing test profile"
    assert manifest["diagnostics"]["summary"] == {"errors": 0, "infos": 0, "warnings": 0}
    assert manifest["native_fabrication"]["coverage"] == {
        "classification": "complete",
        "fab_critical_loss": False,
    }
    assert manifest["native_fabrication"]["warnings"] == []
    assert [item["filename"] for item in manifest["native_fabrication"]["files"]] == [
        "Control.GTL",
        "Control.GBL",
        "Control.GTS",
        "Control.GBS",
        "Control.GTO",
        "Control.GTP",
        "Control.GKO",
        "Control-PTH.TXT",
        "Control-NPTH.TXT",
    ]

    artifact_paths = {item["kind"]: item["path"] for item in manifest["artifacts"]}
    for kind in ("project_manifest", "bom", "bom_csv", "cpl", "cpl_csv", "diagnostics"):
        assert output.joinpath(artifact_paths[kind]).is_file()

    native_paths = {
        item["filename"]: output.joinpath(item["path"])
        for item in manifest["native_fabrication"]["files"]
    }
    assert "%TF.FileFunction,Copper,L1,Top*%" in native_paths["Control.GTL"].read_text(
        encoding="utf-8"
    )
    assert "%TF.FileFunction,Soldermask,Top*%" in native_paths["Control.GTS"].read_text(
        encoding="utf-8"
    )
    assert "%TF.FileFunction,Paste,Top*%" in native_paths["Control.GTP"].read_text(
        encoding="utf-8"
    )
    assert "%TF.FileFunction,Legend,Top*%" in native_paths["Control.GTO"].read_text(
        encoding="utf-8"
    )
    assert "%TF.FileFunction,Profile,NP*%" in native_paths["Control.GKO"].read_text(
        encoding="utf-8"
    )
    assert ";TYPE=PLATED" in native_paths["Control-PTH.TXT"].read_text(encoding="utf-8")
    assert ";TYPE=NON_PLATED" in native_paths["Control-NPTH.TXT"].read_text(encoding="utf-8")

    inspection = output / "manufacturing" / "inspection.html"
    inspection_text = inspection.read_text(encoding="utf-8")
    assert "Native fabrication inspection" in inspection_text
    assert "Control.GTL" in inspection_text
    assert "Control-PTH.TXT" in inspection_text

    first = _directory_bytes(output)
    assert (
        main(
            [
                "export",
                "manufacturing",
                "--json",
                "--output",
                str(output),
                "--project",
                str(root),
            ]
        )
        == 0
    )
    _read_stdout_json(capsys)
    assert _directory_bytes(output) == first


def test_export_manufacturing_refuses_fab_critical_native_loss_without_writing(
    tmp_path, capsys
):
    root = tmp_path / "board"
    output = tmp_path / "manufacturing-package"
    _write_project(
        root,
        extra_config="""
[manufacturing]
profile = "profiles/generic.volt.json"
""",
    )
    _write_manufacturing_profile(root)
    _write_manufacturing_entrypoint(root, lossy=True)

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--json",
                "--output",
                str(output),
                "--project",
                str(root),
            ]
        )
        == 1
    )

    payload = _read_stdout_json(capsys)
    assert payload["status"] == "native-fabrication-loss"
    assert payload["written"] is False
    assert payload["native_fabrication"]["coverage"] == {
        "classification": "fab-critical-loss",
        "fab_critical_loss": True,
    }
    assert [warning["construct"] for warning in payload["native_fabrication"]["warnings"]] == [
        "board.feature.hole.finished_diameter"
    ]
    assert not output.exists()


def test_export_manufacturing_reports_board_selector_errors_without_writing(
    tmp_path, capsys
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

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--output",
                str(output),
                "--project",
                str(root),
            ]
        )
        == 2
    )
    ambiguous = capsys.readouterr()
    assert ambiguous.out == ""
    assert "Project result has multiple boards; pass --board" in ambiguous.err
    assert not output.exists()

    assert (
        main(
            [
                "export",
                "manufacturing",
                "--output",
                str(output),
                "--board",
                "missing",
                "--project",
                str(root),
            ]
        )
        == 2
    )
    missing = capsys.readouterr()
    assert missing.out == ""
    assert "No board named 'missing'" in missing.err
    assert not output.exists()


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
    board = design.board("Main")
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
    board = design.board("Main")
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
