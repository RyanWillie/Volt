import sys
from pathlib import Path

import pytest

from volt.cli import CliError, discover_project, load_project_config, main


def _write_project(root: Path, entrypoint: str = "project_entry:main", paths: str = ""):
    root.mkdir(parents=True, exist_ok=True)
    root.joinpath("volt.toml").write_text(
        f"""[project]
entrypoint = "{entrypoint}"

[paths]
{paths}
""",
        encoding="utf-8",
    )


def _write_entrypoint(root: Path, body: str):
    root.joinpath("project_entry.py").write_text(body, encoding="utf-8")
    sys.modules.pop("project_entry", None)


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
