import hashlib
import json
import os
import sys
from pathlib import Path

import pytest

import volt
from test_cli_simulate import (
    _copy_project,
    _circuit,
    _payload,
    _run,
    _write_request,
)


def _fake_ngspice(tmp_path: Path, run_body: str, *, sentinel: Path | None = None) -> Path:
    if os.name == "nt":
        pytest.skip("fake executable uses a POSIX shebang")
    directory = tmp_path / "fake ngspice 46"
    directory.mkdir(exist_ok=True)
    executable = directory / "ngspice"
    sentinel_body = "" if sentinel is None else f"Path({str(sentinel)!r}).touch()"
    executable.write_text(
        f"""#!{sys.executable}
from pathlib import Path
import sys

if "--version" in sys.argv:
    print("** ngspice-46 : Circuit level simulation program")
    raise SystemExit(0)

{sentinel_body}
{run_body}
""",
        encoding="utf-8",
    )
    executable.chmod(0o755)
    return executable


def _analysis_and_output(bundle: Path, request_path: Path):
    input = _circuit(bundle, "divider").electrical_input()
    request = volt.DcRequest.from_json(input, request_path.read_bytes())
    compiled = volt.compile_electrical(request)
    assert compiled.complete
    analysis = volt.prepare_ngspice_dc(compiled.model)
    assert analysis.complete
    headers = json.loads(analysis.to_json())["mapping"]["output"]["headers"]
    output = " ".join(headers) + "\n0 0 5 2.5 -0.0025\n"
    return analysis, output.encode()


@pytest.fixture
def ngspice_project(tmp_path):
    project = _copy_project(tmp_path)
    bundle = tmp_path / "linear-dc.volt"
    built = _run(
        tmp_path,
        "build",
        "--project",
        str(project),
        "--output",
        str(bundle),
        "--json",
    )
    assert built.returncode == 0, built
    request = tmp_path / "request.json"
    _write_request(bundle, request)
    return project, bundle, request


def test_source_and_offline_bundle_use_same_native_ngspice_mapping(
    ngspice_project, tmp_path
):
    project, bundle, request = ngspice_project
    analysis, output_bytes = _analysis_and_output(bundle, request)
    executable = _fake_ngspice(
        tmp_path,
        f'Path("volt-dc-output.txt").write_bytes({output_bytes!r})',
    )
    source_sentinel = tmp_path / "source-executions.txt"
    environment = os.environ.copy()
    environment["VOLT_TEST_SOURCE_SENTINEL"] = str(source_sentinel)
    environment["VOLT_TEST_FAIL_BOARD"] = "1"

    source_output = tmp_path / "source-ngspice"
    source = _run(
        tmp_path,
        "simulate",
        "--project",
        str(project),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(source_output),
        "--backend",
        "ngspice",
        "--ngspice",
        str(executable),
        "--json",
        environment=environment,
    )
    assert source.returncode == 0, source
    assert source_sentinel.read_text().splitlines() == ["executed"]
    source_payload = _payload(source)

    project.joinpath("project_entry.py").write_text(
        "raise RuntimeError('bundle backend imported poisoned source')\n",
        encoding="utf-8",
    )
    project.joinpath("volt.toml").write_text("invalid TOML", encoding="utf-8")
    bundle_output = tmp_path / "bundle-ngspice"
    bundled = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(bundle_output),
        "--backend",
        "ngspice",
        "--ngspice",
        str(executable),
        "--json",
    )
    assert bundled.returncode == 0, bundled
    assert source_sentinel.read_text().splitlines() == ["executed"]
    bundle_payload = _payload(bundled)

    expected_artifacts = {
        "request.json",
        "compile-report.json",
        "ngspice-analysis.json",
        "deck.cir",
        "ngspice-process.json",
        "ngspice-output.txt",
        "solve-report.json",
        "solution.json",
    }
    for payload, directory in (
        (source_payload, source_output),
        (bundle_payload, bundle_output),
    ):
        assert payload["ok"] is True
        assert payload["status"] == "success"
        assert payload["backend"] == "ngspice"
        assert set(payload["artifacts"]) == expected_artifacts
        assert set(path.name for path in directory.iterdir()) == expected_artifacts
        assert payload["backend_report"]["deck_identity"] == analysis.deck_identity.value
        assert payload["backend_report"]["mapping"]["identity"] == (
            analysis.mapping_identity.value
        )
        assert payload["process"]["version"] == "ngspice-46"
        assert payload["process"]["command"][-2:] == ["-n", "deck.cir"]
        assert payload["process"]["output_content_digest"] == (
            "sha256:"
            + hashlib.sha256((directory / "ngspice-output.txt").read_bytes()).hexdigest()
        )
        assert payload["solve_report"]["outcome"] == "success"
        solution = json.loads((directory / "solution.json").read_text())
        probes = {
            probe["key"]: probe["value"]
            for probe in solution["observations"]["probes"]
        }
        assert probes["midpoint-voltage"]["si"] == pytest.approx(2.5)

    assert source_payload["compile_report"] == bundle_payload["compile_report"]
    assert source_payload["backend_report"] == bundle_payload["backend_report"]
    assert source_payload["solve_report"] == bundle_payload["solve_report"]


def test_default_native_backend_remains_external_solver_free(ngspice_project, tmp_path):
    _project, bundle, request = ngspice_project
    output = tmp_path / "native-output"

    completed = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(output),
        "--json",
    )

    assert completed.returncode == 0, completed
    assert _payload(completed)["status"] == "success"


@pytest.mark.parametrize(
    "backend_arguments",
    [
        ("--backend", "ngspice"),
        ("--ngspice", "unused"),
    ],
)
def test_backend_specific_options_are_required_and_rejected_exactly(
    ngspice_project, tmp_path, backend_arguments
):
    _project, bundle, request = ngspice_project
    output = tmp_path / "invalid-options"

    completed = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(output),
        *backend_arguments,
        "--json",
    )

    assert completed.returncode == 2
    payload = _payload(completed)
    assert payload["ok"] is False
    assert payload["status"] == "error"
    assert not output.exists()


def test_incomplete_compile_does_not_even_inspect_ngspice(ngspice_project, tmp_path):
    _project, bundle, _request = ngspice_project
    request = tmp_path / "incomplete.json"
    _write_request(bundle, request, kind="incomplete")
    sentinel = tmp_path / "ngspice-ran"
    executable = _fake_ngspice(
        tmp_path,
        'raise AssertionError("incomplete compile launched ngspice")',
        sentinel=sentinel,
    )
    output = tmp_path / "incomplete-ngspice"

    completed = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(output),
        "--backend",
        "ngspice",
        "--ngspice",
        str(executable),
        "--json",
    )

    assert completed.returncode == 1, completed
    payload = _payload(completed)
    assert payload["status"] == "incomplete"
    assert payload["backend"] == "ngspice"
    assert payload["backend_report"] is None
    assert payload["process"] is None
    assert set(path.name for path in output.iterdir()) == {
        "request.json",
        "compile-report.json",
    }
    assert not sentinel.exists()


def test_missing_ngspice_binary_is_command_error_without_publication(
    ngspice_project, tmp_path
):
    _project, bundle, request = ngspice_project
    output = tmp_path / "missing-binary-output"

    completed = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(output),
        "--backend",
        "ngspice",
        "--ngspice",
        str(tmp_path / "missing-ngspice"),
        "--json",
    )

    assert completed.returncode == 2
    payload = _payload(completed)
    assert payload["status"] == "error"
    assert not output.exists()


def test_wrong_version_is_command_error_without_published_analysis(ngspice_project, tmp_path):
    _project, bundle, request = ngspice_project
    executable = _fake_ngspice(
        tmp_path,
        'raise AssertionError("wrong version reached deck execution")',
    )
    executable.write_text(
        executable.read_text().replace("ngspice-46", "ngspice-45"),
        encoding="utf-8",
    )
    output = tmp_path / "wrong-version"

    completed = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(output),
        "--backend",
        "ngspice",
        "--ngspice",
        str(executable),
        "--json",
    )

    assert completed.returncode == 2
    payload = _payload(completed)
    assert payload["status"] == "error"
    assert not output.exists()


def test_native_parser_rejects_malformed_process_output_without_publication(
    ngspice_project, tmp_path
):
    _project, bundle, request = ngspice_project
    executable = _fake_ngspice(
        tmp_path,
        'Path("volt-dc-output.txt").write_bytes(b"not native output")',
    )
    output = tmp_path / "malformed-output"

    completed = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(output),
        "--backend",
        "ngspice",
        "--ngspice",
        str(executable),
        "--json",
    )

    assert completed.returncode == 2
    payload = _payload(completed)
    assert payload["status"] == "error"
    assert "process evidence" in payload["error"]["message"]
    assert not output.exists()


def test_relative_executable_path_is_resolved_before_source_worker_dispatch(
    ngspice_project, tmp_path
):
    project, bundle, request = ngspice_project
    _analysis, output_bytes = _analysis_and_output(bundle, request)
    executable = _fake_ngspice(
        tmp_path,
        f'Path("volt-dc-output.txt").write_bytes({output_bytes!r})',
    )
    output = tmp_path / "relative-source-output"

    completed = _run(
        tmp_path,
        "simulate",
        "--project",
        project.name,
        "--design",
        "divider",
        "--request",
        request.name,
        "--output",
        output.name,
        "--backend",
        "ngspice",
        "--ngspice",
        str(executable.relative_to(tmp_path)),
        "--json",
    )

    assert completed.returncode == 0, completed
    payload = _payload(completed)
    assert payload["process"]["executable"] == str(executable.resolve())
    assert payload["output"] == str(output.resolve())
