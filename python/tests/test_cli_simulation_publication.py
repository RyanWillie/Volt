import json
from pathlib import Path

import pytest

from volt.cli import CliError
from volt.cli import _simulation


class _Request:
    @staticmethod
    def from_json(input, data):
        assert input == "native-input"
        assert data == b'{"submitted":true}'
        return _Request()

    def to_json(self):
        return '{"canonical":true}'


class _Solution:
    def to_json(self):
        return '{"solution":true}'


class _SolveReport:
    def __init__(self, success):
        self.success = success
        self.solution = _Solution() if success else None

    def to_json(self):
        return json.dumps({"outcome": "success" if self.success else "rank_deficient"})


class _CompileReport:
    model = "native-model"

    def __init__(self, complete):
        self.complete = complete

    def to_json(self):
        return json.dumps(
            {"complete": self.complete, "diagnostics": [{"code": "native"}]}
        )


@pytest.fixture
def request_path(tmp_path):
    path = tmp_path / "submitted-request.json"
    path.write_bytes(b'{"submitted":true}')
    return path


def _install_native(monkeypatch, *, complete=True, success=True):
    calls = {"compile": 0, "solve": 0}

    def compile_electrical(request):
        assert isinstance(request, _Request)
        calls["compile"] += 1
        return _CompileReport(complete)

    def solve_dc(model):
        assert model == "native-model"
        calls["solve"] += 1
        return _SolveReport(success)

    monkeypatch.setattr(_simulation, "DcRequest", _Request)
    monkeypatch.setattr(_simulation, "compile_electrical", compile_electrical)
    monkeypatch.setattr(_simulation, "solve_dc", solve_dc)
    return calls


def test_success_publishes_only_precomputed_native_artifacts(
    tmp_path, request_path, monkeypatch
):
    calls = _install_native(monkeypatch)
    output = tmp_path / "new" / "parents" / "published"

    payload, exit_code = _simulation.execute_dc(
        "native-input",
        request_path,
        output,
        design="divider",
        source={"kind": "bundle", "path": "/input/project.volt"},
    )

    assert exit_code == 0
    assert calls == {"compile": 1, "solve": 1}
    assert payload == {
        "ok": True,
        "status": "success",
        "design": "divider",
        "source": {"kind": "bundle", "path": "/input/project.volt"},
        "output": str(output),
        "written": True,
        "artifacts": {
            name: str(output / name)
            for name in (
                "request.json",
                "compile-report.json",
                "solve-report.json",
                "solution.json",
            )
        },
        "compile_report": {
            "complete": True,
            "diagnostics": [{"code": "native"}],
        },
        "solve_report": {"outcome": "success"},
    }
    assert (output / "request.json").read_bytes() == b'{"canonical":true}'
    assert json.loads((output / "compile-report.json").read_bytes())["complete"]
    assert (
        json.loads((output / "solve-report.json").read_bytes())["outcome"] == "success"
    )
    assert (output / "solution.json").read_bytes() == b'{"solution":true}'


def test_incomplete_compile_is_published_without_calling_solver(
    tmp_path, request_path, monkeypatch
):
    calls = _install_native(monkeypatch, complete=False)
    output = tmp_path / "incomplete"

    payload, exit_code = _simulation.execute_dc(
        "native-input", request_path, output, design="d", source={"kind": "project"}
    )

    assert exit_code == 1
    assert calls == {"compile": 1, "solve": 0}
    assert payload["status"] == "incomplete"
    assert payload["ok"] is False
    assert payload["solve_report"] is None
    assert set(payload["artifacts"]) == {"request.json", "compile-report.json"}


def test_unsuccessful_solve_has_report_but_no_solution(
    tmp_path, request_path, monkeypatch
):
    _install_native(monkeypatch, success=False)
    output = tmp_path / "failed"

    payload, exit_code = _simulation.execute_dc(
        "native-input", request_path, output, design="d", source={"kind": "bundle"}
    )

    assert exit_code == 1
    assert payload["status"] == "failed"
    assert payload["solve_report"] == {"outcome": "rank_deficient"}
    assert set(payload["artifacts"]) == {
        "request.json",
        "compile-report.json",
        "solve-report.json",
    }
    assert not (output / "solution.json").exists()


def test_existing_and_dangling_output_are_rejected(tmp_path):
    existing = tmp_path / "existing"
    existing.mkdir()
    with pytest.raises(CliError) as directory_error:
        _simulation.validate_output(existing)
    assert directory_error.value.code == "simulation-output-exists"

    dangling = tmp_path / "dangling"
    dangling.symlink_to(tmp_path / "missing")
    with pytest.raises(CliError) as symlink_error:
        _simulation.validate_output(dangling)
    assert symlink_error.value.code == "simulation-output-exists"


def test_native_serialization_failure_never_creates_staging(
    tmp_path, request_path, monkeypatch
):
    _install_native(monkeypatch)

    def fail_compile(_request):
        class BrokenReport:
            complete = False

            def to_json(self):
                raise RuntimeError("serialization failed")

        return BrokenReport()

    monkeypatch.setattr(_simulation, "compile_electrical", fail_compile)
    output = tmp_path / "serialization"

    with pytest.raises(CliError) as caught:
        _simulation.execute_dc(
            "native-input", request_path, output, design="d", source={}
        )

    assert caught.value.code == "native-dc-execution-failed"
    assert not output.exists()
    assert not list(tmp_path.glob(".serialization.*"))


def test_write_failure_cleans_owned_staging(tmp_path, request_path, monkeypatch):
    _install_native(monkeypatch)
    output = tmp_path / "write-failure"
    original_write_bytes = Path.write_bytes

    def fail_stage_write(path, data):
        if path.parent.name.startswith(".write-failure."):
            raise OSError("write failed")
        return original_write_bytes(path, data)

    monkeypatch.setattr(Path, "write_bytes", fail_stage_write)
    with pytest.raises(CliError) as caught:
        _simulation.execute_dc(
            "native-input", request_path, output, design="d", source={}
        )

    assert caught.value.code == "simulation-publication-failed"
    assert not output.exists()
    assert not list(tmp_path.glob(".write-failure.*"))


def test_cleanup_failure_reports_both_errors_and_retained_staging_path(
    tmp_path, request_path, monkeypatch
):
    _install_native(monkeypatch)
    output = tmp_path / "cleanup-failure"
    staged = None

    def fail_publish(stage, _destination):
        nonlocal staged
        staged = stage
        raise OSError("publication failed")

    def fail_cleanup(path):
        assert path == staged
        raise OSError("cleanup failed")

    monkeypatch.setattr(_simulation, "_publish_directory", fail_publish)
    monkeypatch.setattr(_simulation.shutil, "rmtree", fail_cleanup)

    with pytest.raises(CliError) as caught:
        _simulation.execute_dc(
            "native-input", request_path, output, design="d", source={}
        )

    assert caught.value.code == "simulation-publication-failed"
    assert "publication failed" in str(caught.value)
    assert "cleanup failed" in str(caught.value)
    assert f"retained staging directory {staged}" in str(caught.value)
    assert staged is not None
    assert staged.is_dir()
    assert not output.exists()


def test_publication_collision_preserves_racing_destination(
    tmp_path, request_path, monkeypatch
):
    _install_native(monkeypatch)
    output = tmp_path / "collision"
    publish = _simulation._publish_directory

    def race(stage, destination):
        destination.mkdir()
        (destination / "owner.txt").write_text("other process")
        publish(stage, destination)

    monkeypatch.setattr(_simulation, "_publish_directory", race)
    with pytest.raises(CliError) as caught:
        _simulation.execute_dc(
            "native-input", request_path, output, design="d", source={}
        )

    assert caught.value.code == "simulation-output-exists"
    assert (output / "owner.txt").read_text() == "other process"
    assert sorted(path.name for path in output.iterdir()) == ["owner.txt"]
    assert not list(tmp_path.glob(".collision.*"))


def test_publication_does_not_replace_empty_directory_created_during_race(
    tmp_path, request_path, monkeypatch
):
    _install_native(monkeypatch)
    output = tmp_path / "empty-collision"
    publish = _simulation._publish_directory

    def race(stage, destination):
        destination.mkdir()
        publish(stage, destination)

    monkeypatch.setattr(_simulation, "_publish_directory", race)
    with pytest.raises(CliError) as caught:
        _simulation.execute_dc(
            "native-input", request_path, output, design="d", source={}
        )

    assert caught.value.code == "simulation-output-exists"
    assert output.is_dir()
    assert list(output.iterdir()) == []
    assert not list(tmp_path.glob(".empty-collision.*"))


def test_request_read_and_binding_errors_publish_nothing(
    tmp_path, request_path, monkeypatch
):
    output_parent = tmp_path / "nested" / "parents"
    output = output_parent / "bad-request"
    with pytest.raises(CliError) as read_error:
        _simulation.execute_dc(
            "native-input", tmp_path / "missing.json", output, design="d", source={}
        )
    assert read_error.value.code == "dc-request-read-failed"
    assert not output_parent.exists()

    class RejectingRequest:
        @staticmethod
        def from_json(_input, _data):
            raise RuntimeError("unsupported request contract")

    monkeypatch.setattr(_simulation, "DcRequest", RejectingRequest)
    with pytest.raises(CliError) as binding_error:
        _simulation.execute_dc(
            "native-input", request_path, output, design="d", source={}
        )
    assert binding_error.value.code == "native-dc-execution-failed"
    assert not output_parent.exists()
    assert not output.exists()
