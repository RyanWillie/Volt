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
    assert manifest["run"]["status"] == "clean"
    assert manifest["run"]["profile"] == "default"
    logical = next(
        artifact
        for artifact in manifest["artifacts"]
        if artifact["kind"] == "logical_model"
    )
    assert output.joinpath(logical["path"]).is_file()


def test_build_json_persists_failed_project_evidence(tmp_path, capsys):
    root = tmp_path / "board"
    output = tmp_path / "bundle"
    _write_project(root)
    _write_bad_led_entrypoint(root)

    assert (
        main(["build", "--json", "--output", str(output), "--project", str(root)])
        == 1
    )

    payload = _read_stdout_json(capsys)
    assert payload["format"] == "volt.cli-result"
    assert payload["schema_version"] == 1
    assert payload["command"] == "build"
    assert payload["status"] == "failed"
    assert payload["ok"] is False
    assert payload["structurally_valid"] is True
    assert payload["target_ready"] is False
    assert payload["written"] is True
    assert payload["output"] == str(output)
    assert payload["diagnostics"]["summary"]["errors"] > 0
    assert output.joinpath("manifest.volt.json").is_file()


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

    assert main(["check", "--json", "--project", str(root)]) == 0
    payload = _read_stdout_json(capsys)
    assert payload["command"] == "check"
    assert payload["status"] == "clean"

    assert main(["init", "blink", "--profile", "jlcpcb"]) == 2
    refused = capsys.readouterr()
    assert refused.out == ""
    assert "Refusing to overwrite existing file" in refused.err


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
    assert inspection["schema_version"] == 2
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


def test_library_commands_run_purpose_built_library_end_to_end(tmp_path, capsys):
    source = tmp_path / "architecture-fixture-library"
    bundle = tmp_path / "standard.voltlib"
    _write_library(source)

    assert main(["library", "check", str(source), "--json"]) == 0
    check = _read_stdout_json(capsys)
    assert check["ok"] is True
    assert [part["name"] for part in check["parts"]] == ["R-1K"]

    assert main(["library", "build", str(source), "--output", str(bundle)]) == 0
    capsys.readouterr()
    assert main(["library", "test", str(bundle), "--json"]) == 0
    tested = _read_stdout_json(capsys)
    assert tested["parts"] == [part["name"] for part in check["parts"]]

    assert main(["library", "inspect", str(bundle), "--json"]) == 0
    inspection = _read_stdout_json(capsys)
    assert inspection["library"]["namespace"] == "test.cli.library"


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
