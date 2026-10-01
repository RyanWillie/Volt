import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

import volt
from volt.cli import _simulation


FIXTURE = Path(__file__).parent / "fixtures" / "project_cli" / "linear_dc"
SAMPLE = Path(__file__).parents[2] / "samples" / "linear_dc"


def _copy_project(tmp_path: Path) -> Path:
    project = tmp_path / "linear-dc"
    shutil.copytree(FIXTURE, project)
    return project


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


def _payload(completed: subprocess.CompletedProcess[str]) -> dict:
    assert len(completed.stdout.splitlines()) == 1, completed
    payload = json.loads(completed.stdout)
    assert payload["format"] == "volt.cli-result"
    assert payload["schema_version"] == 1
    assert payload["command"] == "simulate"
    return payload


def _circuit(bundle: Path, design: str):
    circuits = volt.ProjectBundle.open(bundle).graph.loaded_project.circuits
    return next(circuit for circuit in circuits if circuit.design == design)


def _write_request(bundle: Path, output: Path, design: str = "divider", kind: str = "complete"):
    input = _circuit(bundle, design).electrical_input()
    if kind == "incomplete":
        request = volt.DcRequest("incomplete", input)
    elif design == "floating":
        request = volt.DcRequest("floating", input, reference=input.nets[0])
    elif design == "unsupported":
        request = volt.DcRequest(
            "unsupported",
            input,
            reference=input.nets[1],
            sources=[
                volt.DcVoltageSource(
                    "supply-5v",
                    input.nets[0],
                    input.nets[1],
                    volt.Quantity(volt.UnitDimension.VOLTAGE, 5),
                )
            ],
        )
    elif kind == "inconsistent":
        supply, midpoint, reference = input.nets
        request = volt.DcRequest(
            "inconsistent-loop",
            input,
            reference=reference,
            sources=[
                volt.DcVoltageSource(
                    "supply-midpoint",
                    supply,
                    midpoint,
                    volt.Quantity(volt.UnitDimension.VOLTAGE, 2),
                ),
                volt.DcVoltageSource(
                    "midpoint-reference",
                    midpoint,
                    reference,
                    volt.Quantity(volt.UnitDimension.VOLTAGE, 2),
                ),
                volt.DcVoltageSource(
                    "supply-reference",
                    supply,
                    reference,
                    volt.Quantity(volt.UnitDimension.VOLTAGE, 5),
                ),
            ],
        )
    else:
        supply, midpoint, reference = input.nets
        request = volt.DcRequest(
            "divider-operating-point",
            input,
            reference=reference,
            sources=[
                volt.DcVoltageSource(
                    "supply-5v",
                    supply,
                    reference,
                    volt.Quantity(volt.UnitDimension.VOLTAGE, 5),
                )
            ],
            probes=[volt.DcVoltageProbe("midpoint-voltage", midpoint, reference)],
        )
    output.write_text(request.to_json(), encoding="utf-8")
    return request


@pytest.fixture
def linear_project(tmp_path):
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
    assert bundle.is_dir()
    return project, bundle


def _assert_published(payload: dict, output: Path, expected: set[str]):
    assert payload["written"] is True
    assert payload["output"] == str(output.resolve())
    assert set(payload["artifacts"]) == expected
    for name, path in payload["artifacts"].items():
        assert path == str((output / name).resolve())
        assert Path(path).read_bytes()
    assert set(item.name for item in output.iterdir()) == expected
    assert json.loads((output / "compile-report.json").read_text()) == payload["compile_report"]
    if "solve-report.json" in expected:
        assert json.loads((output / "solve-report.json").read_text()) == payload["solve_report"]
    else:
        assert payload["solve_report"] is None


def test_python_authored_source_and_source_free_bundle_have_native_dc_parity(
    linear_project, tmp_path
):
    project, bundle = linear_project
    request_path = tmp_path / "request.json"
    request = _write_request(bundle, request_path)
    sentinel = tmp_path / "source-executions.txt"
    environment = os.environ.copy()
    environment["VOLT_TEST_SOURCE_SENTINEL"] = str(sentinel)
    environment["VOLT_TEST_FAIL_BOARD"] = "1"

    source_output = tmp_path / "source-dc"
    source = _run(
        tmp_path,
        "simulate",
        "--project",
        str(project),
        "--design",
        "divider",
        "--request",
        str(request_path),
        "--output",
        str(source_output),
        "--json",
        environment=environment,
    )
    assert source.returncode == 0, source
    assert "linear-dc fixture source stdout" in source.stderr
    assert sentinel.read_text().splitlines() == ["executed"]
    source_payload = _payload(source)
    assert source_payload["ok"] is True
    assert source_payload["status"] == "success"
    assert source_payload["design"] == "divider"
    assert source_payload["source"]["kind"] == "project"
    _assert_published(
        source_payload,
        source_output,
        {"request.json", "compile-report.json", "solve-report.json", "solution.json"},
    )

    project.joinpath("project_entry.py").write_text(
        "raise RuntimeError('bundle simulation imported poisoned source')\n",
        encoding="utf-8",
    )
    project.joinpath("volt.toml").write_text("not valid TOML", encoding="utf-8")
    bundle_output = tmp_path / "bundle-dc"
    bundled = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "divider",
        "--request",
        str(request_path),
        "--output",
        str(bundle_output),
        "--json",
        environment={"PATH": "", "PYTHONPATH": os.environ.get("PYTHONPATH", "")},
    )
    assert bundled.returncode == 0, bundled
    assert bundled.stderr == ""
    assert sentinel.read_text().splitlines() == ["executed"]
    bundle_payload = _payload(bundled)
    assert bundle_payload["ok"] is True
    assert bundle_payload["status"] == "success"
    assert bundle_payload["design"] == "divider"
    assert bundle_payload["source"]["kind"] == "bundle"
    assert bundle_payload["source"]["bundle_digest"]
    _assert_published(
        bundle_payload,
        bundle_output,
        {"request.json", "compile-report.json", "solve-report.json", "solution.json"},
    )

    assert source_payload["compile_report"]["input"] == bundle_payload["compile_report"]["input"]
    assert source_payload["compile_report"]["model"]["identity"] == (
        bundle_payload["compile_report"]["model"]["identity"]
    )
    assert source_payload["solve_report"]["analysis_identity"] == (
        bundle_payload["solve_report"]["analysis_identity"]
    )
    assert (source_output / "request.json").read_text() == request.to_json()
    source_solution = json.loads((source_output / "solution.json").read_text())
    bundle_solution = json.loads((bundle_output / "solution.json").read_text())
    source_observations = source_solution["observations"]
    bundle_observations = bundle_solution["observations"]
    assert [node["potential"]["si"] for node in source_observations["nodes"]] == pytest.approx(
        [node["potential"]["si"] for node in bundle_observations["nodes"]]
    )
    assert source_observations["probes"][0]["value"]["si"] == pytest.approx(2.5)


def test_default_discovery_and_relative_paths_are_resolved_in_calling_context(
    linear_project, tmp_path
):
    project, bundle = linear_project
    request = project / "request.json"
    _write_request(bundle, request)
    nested = project / "nested" / "working"
    nested.mkdir(parents=True)

    completed = _run(
        nested,
        "simulate",
        "--design",
        "divider",
        "--request",
        "../../request.json",
        "--output",
        "relative-output",
        "--json",
    )

    assert completed.returncode == 0, completed
    payload = _payload(completed)
    assert payload["output"] == str((nested / "relative-output").resolve())
    assert (nested / "relative-output" / "solution.json").is_file()


@pytest.mark.parametrize("selector", ["missing", "DIVIDER", ""])
def test_multi_design_selection_is_exact_without_fallback(linear_project, tmp_path, selector):
    _project, bundle = linear_project
    request = tmp_path / "request.json"
    _write_request(bundle, request)
    output = tmp_path / f"unknown-{selector or 'empty'}"

    completed = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        selector,
        "--request",
        str(request),
        "--output",
        str(output),
        "--json",
    )

    assert completed.returncode == 2
    payload = _payload(completed)
    assert payload["ok"] is False
    assert payload["status"] == "error"
    assert not output.exists()


@pytest.mark.parametrize("change", ["VOLT_TEST_CHANGED_LOGICAL", "VOLT_TEST_CHANGED_PART"])
def test_source_rejects_stale_logical_or_selected_part_closure(
    linear_project, tmp_path, change
):
    project, bundle = linear_project
    request = tmp_path / "request.json"
    original = _write_request(bundle, request).to_json()
    output = tmp_path / f"stale-{change}"
    environment = os.environ.copy()
    environment[change] = "1"

    completed = _run(
        tmp_path,
        "simulate",
        "--project",
        str(project),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(output),
        "--json",
        environment=environment,
    )

    assert completed.returncode == 2
    assert _payload(completed)["status"] == "error"
    assert request.read_text() == original
    assert not output.exists()


@pytest.mark.parametrize(
    "contents",
    [b"not json", b"{}", b'{"version":999}'],
)
def test_malformed_or_unsupported_request_does_not_publish(
    linear_project, tmp_path, contents
):
    _project, bundle = linear_project
    request = tmp_path / "bad-request.json"
    request.write_bytes(contents)
    output = tmp_path / "bad-output"

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

    assert completed.returncode == 2
    payload = _payload(completed)
    assert payload["status"] == "error"
    assert payload["ok"] is False
    assert not output.exists()


def test_incomplete_native_compile_publishes_no_solve_or_solution(linear_project, tmp_path):
    _project, bundle = linear_project
    request = tmp_path / "incomplete.json"
    _write_request(bundle, request, kind="incomplete")
    output = tmp_path / "incomplete-output"

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

    assert completed.returncode == 1, completed
    payload = _payload(completed)
    assert payload["ok"] is False
    assert payload["status"] == "incomplete"
    assert payload["compile_report"]["complete"] is False
    _assert_published(payload, output, {"request.json", "compile-report.json"})


def test_unsupported_model_coverage_is_incomplete_without_a_solve(linear_project, tmp_path):
    _project, bundle = linear_project
    request = tmp_path / "unsupported.json"
    _write_request(bundle, request, design="unsupported")
    output = tmp_path / "unsupported-output"

    completed = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "unsupported",
        "--request",
        str(request),
        "--output",
        str(output),
        "--json",
    )

    assert completed.returncode == 1, completed
    payload = _payload(completed)
    assert payload["status"] == "incomplete"
    assert any(item["status"] != "supported" for item in payload["compile_report"]["coverage"])
    _assert_published(payload, output, {"request.json", "compile-report.json"})


def test_rank_deficient_native_solve_has_failure_report_and_no_solution(
    linear_project, tmp_path
):
    _project, bundle = linear_project
    request = tmp_path / "floating.json"
    _write_request(bundle, request, design="floating")
    output = tmp_path / "floating-output"

    completed = _run(
        tmp_path,
        "simulate",
        "--bundle",
        str(bundle),
        "--design",
        "floating",
        "--request",
        str(request),
        "--output",
        str(output),
        "--json",
    )

    assert completed.returncode == 1, completed
    payload = _payload(completed)
    assert payload["status"] == "failed"
    assert payload["solve_report"]["outcome"] == "rank_deficient"
    assert payload["solve_report"]["solution"] is None
    _assert_published(
        payload,
        output,
        {"request.json", "compile-report.json", "solve-report.json"},
    )


def test_inconsistent_native_solve_has_failure_report_and_no_solution(
    linear_project, tmp_path
):
    _project, bundle = linear_project
    request = tmp_path / "inconsistent.json"
    _write_request(bundle, request, design="divider", kind="inconsistent")
    output = tmp_path / "inconsistent-output"

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

    assert completed.returncode == 1, completed
    payload = _payload(completed)
    assert payload["status"] == "failed"
    assert payload["solve_report"]["outcome"] == "inconsistent"
    assert payload["solve_report"]["solution"] is None
    _assert_published(
        payload,
        output,
        {"request.json", "compile-report.json", "solve-report.json"},
    )


def test_returned_failed_project_result_is_not_a_whole_project_gate(
    linear_project, tmp_path
):
    project, bundle = linear_project
    request = tmp_path / "request.json"
    _write_request(bundle, request)
    output = tmp_path / "failed-project-result-output"
    environment = os.environ.copy()
    environment["VOLT_TEST_RETURN_RESULT"] = "1"
    environment["VOLT_TEST_FAIL_DESIGN_TEST"] = "1"

    completed = _run(
        tmp_path,
        "simulate",
        "--project",
        str(project),
        "--design",
        "divider",
        "--request",
        str(request),
        "--output",
        str(output),
        "--json",
        environment=environment,
    )

    assert completed.returncode == 0, completed
    assert _payload(completed)["status"] == "success"
    assert (output / "solution.json").is_file()


def test_existing_output_is_rejected_without_mutating_input_or_stale_content(
    linear_project, tmp_path
):
    _project, bundle = linear_project
    request = tmp_path / "request.json"
    original_request = _write_request(bundle, request).to_json()
    output = tmp_path / "existing-output"
    output.mkdir()
    stale = output / "solution.json"
    stale.write_text("stale-success", encoding="utf-8")

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

    assert completed.returncode == 2
    assert _payload(completed)["status"] == "error"
    assert request.read_text() == original_request
    assert stale.read_text() == "stale-success"
    assert list(output.iterdir()) == [stale]


def test_output_cannot_be_inside_input_bundle(linear_project, tmp_path):
    _project, bundle = linear_project
    request = tmp_path / "request.json"
    _write_request(bundle, request)
    output = bundle / "analysis"

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

    assert completed.returncode == 2
    assert _payload(completed)["status"] == "error"
    assert not output.exists()


def test_dangling_destination_symlink_is_an_existing_collision(linear_project, tmp_path):
    _project, bundle = linear_project
    request = tmp_path / "request.json"
    _write_request(bundle, request)
    output = tmp_path / "dangling-output"
    output.symlink_to(tmp_path / "absent-target", target_is_directory=True)

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

    assert completed.returncode == 2
    assert _payload(completed)["status"] == "error"
    assert output.is_symlink()
    assert not output.resolve().exists()


def test_invalid_bundle_and_mutually_exclusive_inputs_fail_without_output(
    linear_project, tmp_path
):
    project, bundle = linear_project
    request = tmp_path / "request.json"
    _write_request(bundle, request)
    invalid = tmp_path / "invalid.volt"
    invalid.write_text("not a bundle", encoding="utf-8")

    for arguments, output in (
        (("--bundle", str(invalid)), tmp_path / "invalid-output"),
        (
            ("--bundle", str(bundle), "--project", str(project)),
            tmp_path / "exclusive-output",
        ),
    ):
        completed = _run(
            tmp_path,
            "simulate",
            *arguments,
            "--design",
            "divider",
            "--request",
            str(request),
            "--output",
            str(output),
            "--json",
        )
        assert completed.returncode == 2
        assert not output.exists()


def test_native_incomplete_compile_never_calls_solver(linear_project, tmp_path, monkeypatch):
    _project, bundle = linear_project
    request = tmp_path / "incomplete.json"
    _write_request(bundle, request, kind="incomplete")

    def fail_solve(_model):
        raise AssertionError("solver called after incomplete native compilation")

    monkeypatch.setattr(_simulation, "solve_dc", fail_solve)
    artifacts, compile_report, solve_report, status, exit_code = _simulation._native_outputs(
        _circuit(bundle, "divider").electrical_input(), request
    )

    assert set(artifacts) == {"request.json", "compile-report.json"}
    assert compile_report["complete"] is False
    assert solve_report is None
    assert status == "incomplete"
    assert exit_code == 1


def test_check_and_build_do_not_implicitly_execute_solver(
    linear_project, tmp_path
):
    project, _bundle = linear_project
    environment = os.environ.copy()
    environment["VOLT_TEST_FORBID_SIMULATION"] = "1"
    checked = _run(
        tmp_path,
        "check",
        "--project",
        str(project),
        "--json",
        environment=environment,
    )
    built = _run(
        tmp_path,
        "build",
        "--project",
        str(project),
        "--output",
        str(tmp_path / "ordinary.volt"),
        "--json",
        environment=environment,
    )

    assert checked.returncode == 0, checked
    assert built.returncode == 0, built
    assert not (tmp_path / "ordinary.volt" / "solution.json").exists()


def test_explicit_python_recipe_solves_the_documented_five_volt_divider(tmp_path):
    output = tmp_path / "python-recipe"

    completed = subprocess.run(
        [sys.executable, str(SAMPLE / "run_dc.py"), str(output)],
        cwd=SAMPLE,
        env={
            **os.environ,
            "PYTHONPATH": os.pathsep.join(
                str(Path(entry).resolve()) for entry in sys.path if entry
            ),
        },
        check=False,
        capture_output=True,
        text=True,
    )

    assert completed.returncode == 0, completed
    assert completed.stdout == ""
    assert completed.stderr == ""
    assert set(path.name for path in output.iterdir()) == {
        "canonicalrequest.json",
        "compile-report.json",
        "divider.volt",
        "solution.json",
        "solve-report.json",
    }
    compile_report = json.loads((output / "compile-report.json").read_text())
    solve_report = json.loads((output / "solve-report.json").read_text())
    solution = json.loads((output / "solution.json").read_text())
    assert compile_report["complete"] is True
    assert solve_report["outcome"] == "success"
    probes = {
        probe["key"]: probe["value"]
        for probe in solution["observations"]["probes"]
    }
    assert probes["midpoint-voltage"] == {
        "dimension": "voltage",
        "si": pytest.approx(2.5),
    }
