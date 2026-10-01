"""Public native transient binding and explicit source/bundle execution acceptance."""
import importlib.util
import json
import math
import os
from pathlib import Path
import shutil

import pytest
import volt
from test_cli_simulate import _run, _payload, _circuit, _assert_published

SAMPLE = Path(__file__).parents[2] / "samples" / "linear_transient"
OPTIONS = ("--h-min", "1e-12", "--h-initial", "5e-5", "--h-max", "1e-4",
           "--max-trials", "100000", "--max-accepted-steps", "100000")
TIGHT = ("--relative-tolerance", "1e-7", "--absolute-voltage-tolerance", "1e-9",
         "--absolute-current-tolerance", "1e-12")
SUCCESS_FILES = {"request.json", "compile-report.json", "solve-report.json", "solution.json"}


def _volts(value):
    return volt.Quantity(volt.UnitDimension.VOLTAGE, value)


def _amps(value):
    return volt.Quantity(volt.UnitDimension.CURRENT, value)


def _options(trials=100000):
    return volt.TransientSolveOptions(volt.seconds(1e-12), volt.seconds(5e-5),
                                     volt.seconds(1e-4), trials, 100000, 1e-7,
                                     _volts(1e-9), _amps(1e-12))


def _load(path):
    spec = importlib.util.spec_from_file_location("volt_transient_example", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def rc_project(tmp_path):
    project_path = tmp_path / "source"
    shutil.copytree(SAMPLE, project_path)
    project = _load(project_path / "main.py").main()
    result = project.run_through(project.design)
    bundle = tmp_path / "startup.volt"
    result.write(bundle)
    return project_path, bundle, result.design("startup")


def _request(input, *, initial=0, waveform=None, state=None, clamp=None):
    supply, output, ground = input.nets
    sources = [volt.TransientVoltageSource("drive", supply, ground,
               waveform if waveform is not None else volt.TransientWaveform(_volts(1)))]
    if clamp is not None:
        sources.append(volt.TransientVoltageSource("clamp", output, ground,
                                                   volt.TransientWaveform(_volts(clamp))))
    if state is None:
        state = [volt.TransientInitialState(input, input.occurrences[1], "body",
                    volt.TransientStorageKind.CAPACITOR_VOLTAGE, _volts(initial))]
    return volt.TransientRequest(
        "rc-startup", input,
        volt.TransientTimeGrid([volt.seconds(t) for t in (0, 0.001, 0.003)]),
        reference=ground, sources=sources,
        probes=[volt.DcVoltageProbe("out", output, ground),
                volt.DcModelElementCurrentProbe("capacitor-current", input.occurrences[1], "body")],
        initial_state=state,
    )


def _execute(cwd, bundle, request_path, output, *options):
    return _run(cwd, "simulate", "--bundle", str(bundle), "--design", "startup",
                "--analysis", "transient", "--backend", "native",
                "--request", str(request_path), "--output", str(output),
                *(options or OPTIONS), "--json")


@pytest.mark.parametrize("times", [[], [0], [0, 0], [0, -1], [1, 2], [0, math.inf], [0, math.nan]])
def test_grid_rejects_malformed_typed_times(times):
    with pytest.raises((volt.InvalidArgumentError, ValueError)):
        volt.TransientTimeGrid([volt.seconds(t) for t in times])


def test_waveform_grid_native_boundaries_and_corner_hold():
    with pytest.raises(volt.InvalidArgumentError):
        volt.TransientTimeGrid([_volts(0), _volts(1)])
    grid = volt.TransientTimeGrid.uniform(volt.seconds(0.003), 7)
    assert grid.times[0].value == 0
    assert grid.times[-1].value == 0.003
    assert all(a.value < b.value for a, b in zip(grid.times, grid.times[1:]))
    waveform = volt.TransientWaveform([
        volt.TransientWaveformKnot(volt.seconds(0), _volts(0)),
        volt.TransientWaveformKnot(volt.seconds(0.0007), _volts(1)),
    ])
    assert waveform.value_at(volt.seconds(0.00035)).value == pytest.approx(0.5)
    assert waveform.value_at(volt.seconds(0.003)).value == 1
    with pytest.raises(volt.InvalidArgumentError):
        volt.TransientWaveform([
            volt.TransientWaveformKnot(volt.seconds(0), _volts(0)),
            volt.TransientWaveformKnot(volt.seconds(0), _volts(1)),
        ])


def test_request_codec_exact_samples_and_authoring_bytes(rc_project):
    _, bundle, design = rc_project
    before = design.to_json()
    input = _circuit(bundle, "startup").electrical_input()
    request = _request(input)
    encoded = request.to_json()
    assert volt.TransientRequest.from_json(input, encoded).to_json() == encoded
    compiled = volt.compile_electrical(request)
    assert compiled.complete
    assert compiled.model.transient_request.to_json() == encoded
    report = volt.solve_transient(compiled.model, _options())
    assert report.success
    assert [sample.time.value for sample in report.solution.samples] == [0, 0.001, 0.003]
    for sample in report.solution.samples:
        probes = {probe.key.value: probe.value.value for probe in sample.probes}
        assert probes["out"] == pytest.approx(1 - math.exp(-sample.time.value / 0.001), abs=1e-3)
        assert probes["capacitor-current"] == pytest.approx(0.001 * math.exp(-sample.time.value / 0.001), abs=1e-6)
    assert 2 * report.accepted_step_count == len(report.accepted_half_steps)
    assert all(step.normalized_error <= 1 for step in report.accepted_half_steps)
    assert report.solve_count >= report.trial_count * 3
    assert report.factorization_count <= report.solve_count
    assert request.to_json() == encoded
    assert design.to_json() == before
    assert volt.prepare_electrical_input(design).identity == input.identity


def test_pwl_off_grid_knot_is_exact_solver_boundary(rc_project):
    _, bundle, _ = rc_project
    input = _circuit(bundle, "startup").electrical_input()
    waveform = volt.TransientWaveform([
        volt.TransientWaveformKnot(volt.seconds(0), _volts(0)),
        volt.TransientWaveformKnot(volt.seconds(0.0007), _volts(1)),
    ])
    report = volt.solve_transient(volt.compile_electrical(_request(input, waveform=waveform)).model,
                                  _options())
    assert report.success
    assert 0.0007 in [step.time.value for step in report.accepted_half_steps]
    assert [sample.time.value for sample in report.solution.samples] == [0, 0.001, 0.003]


def test_dc_helper_copies_provenance_without_solving(rc_project, monkeypatch):
    _, bundle, _ = rc_project
    input = _circuit(bundle, "startup").electrical_input()
    supply, _, ground = input.nets
    dc = volt.DcRequest("bias", input, reference=ground,
                       sources=[volt.DcVoltageSource("drive", supply, ground, _volts(1))])
    solved = volt.solve_dc(volt.compile_electrical(dc).model)
    assert solved.success
    def forbidden(*args, **kwargs):
        raise AssertionError("DC conversion must not execute a solver")
    monkeypatch.setattr(volt, "solve_dc", forbidden)
    monkeypatch.setattr(volt._volt, "solve_dc", forbidden)
    for source in (solved, solved.solution):
        request = _request(input, state=volt.initial_state_from(source))
        assert len(request.initial_state) == 1
        state = request.initial_state[0]
        assert state.value.value == pytest.approx(1)
        assert state.kind == volt.TransientStorageKind.CAPACITOR_VOLTAGE
        assert request.initial_conditions.dc_provenance.analysis_identity == solved.analysis_identity
        assert volt.TransientRequest.from_json(input, request.to_json()).to_json() == request.to_json()
        report = volt.solve_transient(volt.compile_electrical(request).model, _options())
        assert report.success
        assert all(next(probe.value.value for probe in sample.probes if probe.key.value == "out") == pytest.approx(1) for sample in report.solution.samples)


def test_dc_helper_rejects_failed_reports_and_stale_input(rc_project):
    _, bundle, design = rc_project
    input = _circuit(bundle, "startup").electrical_input()
    supply, output, ground = input.nets
    bad = volt.DcRequest("inconsistent-loop", input, reference=ground, sources=[
        volt.DcVoltageSource("a", supply, output, _volts(1)),
        volt.DcVoltageSource("b", output, ground, _volts(1)),
        volt.DcVoltageSource("c", supply, ground, _volts(3)),
    ])
    failed = volt.solve_dc(volt.compile_electrical(bad).model)
    assert not failed.success
    with pytest.raises(volt.InvalidArgumentError):
        volt.initial_state_from(failed)
    bias = volt.DcRequest("bias", input, reference=ground,
                         sources=[volt.DcVoltageSource("drive", supply, ground, _volts(1))])
    copied = volt.initial_state_from(volt.solve_dc(volt.compile_electrical(bias).model))
    design.net("AFTER_CAPTURE")
    fresh = volt.prepare_electrical_input(design)
    with pytest.raises((volt.InvalidArgumentError, volt.CrossReferenceError)):
        volt.TransientRequest("stale", fresh,
            volt.TransientTimeGrid([volt.seconds(0), volt.seconds(0.001)]),
            reference=fresh.nets[2], initial_state=copied)


def test_transient_source_and_poisoned_offline_bundle_parity(rc_project, tmp_path):
    project, bundle, _ = rc_project
    request_path = tmp_path / "request.json"
    request_path.write_text(_request(_circuit(bundle, "startup").electrical_input()).to_json())
    before = request_path.read_bytes()
    bundle_before = {path.relative_to(bundle): path.read_bytes() for path in bundle.rglob("*") if path.is_file()}
    unrelated = tmp_path / "unrelated"
    unrelated.mkdir()
    outputs = []
    for selector, path in (("--project", project), ("--bundle", bundle)):
        if selector == "--bundle":
            (project / "main.py").write_text("raise RuntimeError('bundle imported poisoned source')\n")
            (project / "volt.toml").write_text("invalid TOML")
        output = tmp_path / selector.removeprefix("--")
        run = _run(unrelated, "simulate", selector, str(path), "--design", "startup",
                   "--analysis", "transient", "--backend", "native",
                   "--request", str(request_path), "--output", str(output),
                   *OPTIONS, *TIGHT, "--json",
                   environment=None if selector == "--project" else {"PATH": "", "PYTHONPATH": os.environ.get("PYTHONPATH", "")})
        assert run.returncode == 0, run
        payload = _payload(run)
        assert payload["analysis"] == "transient"
        _assert_published(payload, output, SUCCESS_FILES)
        outputs.append(output)
    for name in SUCCESS_FILES:
        assert (outputs[0] / name).read_bytes() == (outputs[1] / name).read_bytes()
    assert request_path.read_bytes() == before
    assert {path.relative_to(bundle): path.read_bytes() for path in bundle.rglob("*") if path.is_file()} == bundle_before


@pytest.mark.parametrize("kind", ["incomplete", "inconsistent", "unsupported", "budget"])
def test_failed_and_incomplete_cli_publish_evidence_without_solution(rc_project, tmp_path, kind):
    _, bundle, _ = rc_project
    input = _circuit(bundle, "startup").electrical_input()
    request = (_request(input, state=[]) if kind == "incomplete" else
               _request(input, initial=1, clamp=0) if kind == "inconsistent" else
               _request(input, clamp=0) if kind == "unsupported" else _request(input))
    request_path = tmp_path / "request.json"
    request_path.write_text(request.to_json())
    options = OPTIONS if kind != "budget" else (*OPTIONS, "--max-trials", "1")
    output = tmp_path / "result"
    run = _execute(tmp_path, bundle, request_path, output, *options)
    assert run.returncode == 1, run
    payload = _payload(run)
    assert payload["status"] == ("incomplete" if kind == "incomplete" else "failed")
    assert (output / "request.json").exists()
    assert (output / "compile-report.json").exists()
    assert not (output / "solution.json").exists()
    if kind != "incomplete":
        assert (output / "solve-report.json").exists()
        assert payload["solve_report"]["evaluations"]


def test_voltage_probe_overflow_is_timed_analysis_failure_in_cli(tmp_path):
    example = _load(SAMPLE / "main.py")
    resistor = example._part("Rlarge", volt.ResistanceElement, volt.ohms(5e307))
    project = volt.Project("probe-overflow", version="1")
    project.use_library(example.LIBRARY)

    @project.design
    def design():
        result = volt.Design("startup")
        positive, pmid, negative, nmid, ground = (
            result.net(name) for name in ("positive", "pmid", "negative", "nmid", "ground"))
        for index, (from_, to) in enumerate(((positive, pmid), (pmid, ground),
                                            (negative, nmid), (nmid, ground))):
            element = result.instantiate(resistor, ref=f"R{index + 1}").dnp(False)
            from_ += element["A"]
            to += element["B"]
        return result

    result = project.run_through(project.design)
    bundle = tmp_path / "probe.volt"
    result.write(bundle)
    input = _circuit(bundle, "startup").electrical_input()
    positive, _, negative, _, ground = input.nets
    ramp = volt.TransientWaveform([
        volt.TransientWaveformKnot(volt.seconds(0), _amps(0)),
        volt.TransientWaveformKnot(volt.seconds(1), _amps(.9))])
    request = volt.TransientRequest(
        "probe-overflow", input, volt.TransientTimeGrid([volt.seconds(0), volt.seconds(1)]),
        reference=ground,
        sources=[volt.TransientCurrentSource("positive", ground, positive, ramp),
                 volt.TransientCurrentSource("negative", negative, ground, ramp)],
        probes=[volt.DcVoltageProbe("rails", positive, negative)])
    request_path = tmp_path / "request.json"
    request_path.write_text(request.to_json())
    output = tmp_path / "result"
    run = _execute(tmp_path, bundle, request_path, output)
    assert run.returncode == 1, run
    payload = _payload(run)
    assert payload["status"] == "failed"
    report = payload["solve_report"]
    assert report["last_accepted_time"]["si"] == 0
    assert report["evaluations"][-1]["time"]["si"] == 1
    assert any(d["code"] == "TRANSIENT_SOLVE_NUMERICAL_FAILURE" for d in report["diagnostics"])
    assert set(path.name for path in output.iterdir()) == SUCCESS_FILES - {"solution.json"}


@pytest.mark.parametrize("kind", ["malformed", "duplicate-json", "missing-options", "step-order", "zero-budget", "nonfinite"])
def test_cli_command_failures_are_exit_two_without_output(rc_project, tmp_path, kind):
    _, bundle, _ = rc_project
    encoded = _request(_circuit(bundle, "startup").electrical_input()).to_json()
    if kind == "malformed":
        encoded = "not JSON"
    elif kind == "duplicate-json":
        encoded = '{"format":"volt.transient-request",' + encoded[1:]
    request_path = tmp_path / "request.json"
    request_path.write_text(encoded)
    options = OPTIONS
    if kind == "missing-options":
        options = ()
    elif kind == "step-order":
        options = (*OPTIONS, "--h-min", "1")
    elif kind == "zero-budget":
        options = (*OPTIONS, "--max-accepted-steps", "0")
    elif kind == "nonfinite":
        options = (*OPTIONS, "--relative-tolerance", "nan")
    output = tmp_path / "result"
    run = _run(tmp_path, "simulate", "--bundle", str(bundle), "--design", "startup",
               "--analysis", "transient", "--backend", "native", "--request", str(request_path),
               "--output", str(output), *options, "--json")
    assert run.returncode == 2, run
    assert _payload(run)["status"] == "error"
    assert not output.exists()


def test_ngspice_transient_rejected_before_source_execution(tmp_path):
    output = tmp_path / "out"
    run = _run(tmp_path, "simulate", "--analysis", "transient", "--backend", "ngspice",
               "--project", str(tmp_path / "missing"), "--design", "startup",
               "--request", str(tmp_path / "missing.json"), "--output", str(output), "--json")
    assert run.returncode == 2
    assert "unsupported-transient-backend" in run.stdout
    assert not output.exists()


def test_cli_never_overwrites_and_build_does_not_solve(rc_project, tmp_path, monkeypatch):
    project, bundle, _ = rc_project
    request_path = tmp_path / "request.json"
    request_path.write_text(_request(_circuit(bundle, "startup").electrical_input()).to_json())
    output = tmp_path / "existing"
    output.mkdir()
    sentinel = output / "keep.txt"
    sentinel.write_bytes(b"preserved")
    run = _execute(tmp_path, bundle, request_path, output)
    assert run.returncode == 2
    assert sentinel.read_bytes() == b"preserved"
    assert list(output.iterdir()) == [sentinel]
    def forbidden(*args, **kwargs):
        raise AssertionError("Authoring and bundling must not invoke a solver")
    for name in ("solve_dc", "solve_ac", "solve_transient"):
        monkeypatch.setattr(volt, name, forbidden)
        monkeypatch.setattr(volt._volt, name, forbidden)
    authored = _load(project / "main.py").main()
    result = authored.run_through(authored.design)
    ordinary = tmp_path / "ordinary.volt"
    result.write(ordinary)
    assert ordinary.is_dir()
    assert not list(ordinary.rglob("solve-report.json"))
    assert not list(ordinary.rglob("solution.json"))


def test_storage_and_waveform_binding_reject_wrong_targets(rc_project):
    _, bundle, _ = rc_project
    input = _circuit(bundle, "startup").electrical_input()
    for occurrence, element, kind, value in (
        (input.occurrences[0], "body", volt.TransientStorageKind.CAPACITOR_VOLTAGE, _volts(0)),
        (input.occurrences[1], "missing", volt.TransientStorageKind.CAPACITOR_VOLTAGE, _volts(0)),
        (input.occurrences[1], "body", volt.TransientStorageKind.INDUCTOR_CURRENT, _amps(0)),
    ):
        with pytest.raises((volt.InvalidArgumentError, volt.CrossReferenceError)):
            _request(input, state=[volt.TransientInitialState(input, occurrence, element, kind, value)])
    state = volt.TransientInitialState(input, input.occurrences[1], "body",
                                      volt.TransientStorageKind.CAPACITOR_VOLTAGE, _volts(0))
    with pytest.raises(volt.DuplicateNameError):
        _request(input, state=[state, state])
    with pytest.raises(volt.InvalidArgumentError):
        _request(input, waveform=volt.TransientWaveform([
            volt.TransientWaveformKnot(volt.seconds(0), _volts(0)),
            volt.TransientWaveformKnot(volt.seconds(0.004), _volts(1)),
        ]))


def test_executable_rc_sample_runs_native_cli(tmp_path):
    import subprocess
    import sys
    environment = os.environ.copy()
    environment["PYTHONPATH"] = os.pathsep.join(str(Path(entry).resolve()) for entry in sys.path if entry)
    output = tmp_path / "sample"
    run = subprocess.run([sys.executable, str(SAMPLE / "run_transient.py"), str(output)],
                         cwd=tmp_path, env=environment, text=True, capture_output=True, check=False)
    assert run.returncode == 0, run
    assert _payload(run)["status"] == "success"
    solution = json.loads((output / "results" / "solution.json").read_text())
    samples = solution["observations"]["samples"]
    assert [sample["time"]["si"] for sample in samples] == [0, 0.001, 0.003]
    assert samples[1]["probes"][0]["value"]["si"] == pytest.approx(0.632120559, abs=1e-3)
