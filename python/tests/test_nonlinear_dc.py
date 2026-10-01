"""Public native diode authoring, solve and source-free execution contracts."""
import gc
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest
import volt
from volt.cli import _build_parser, CliError
from volt.cli._simulation import nonlinear_options, transient_options


def _sample():
    path = Path(__file__).resolve().parents[2] / "samples/nonlinear_dc/main.py"
    spec = importlib.util.spec_from_file_location("nonlinear_dc_example", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def authored_case():
    sample = _sample()
    project = sample.main()
    result = project.run_through(project.design)
    input = volt.prepare_electrical_input(result.design("diode-bias"))
    request = sample.request_for(input)
    compiled = volt.compile_electrical(request)
    assert compiled.complete
    return sample, result, input, request, compiled.model


def test_diode_native_values_own_lifetimes_and_explicit_polarity():
    sample = _sample()
    model = sample.DIODE.electrical_model
    element = model.elements[0]
    assert isinstance(element, volt.ShockleyDiodeElement)
    assert str(element.from_) == "a" and str(element.to) == "b"
    parameters = element.parameters
    assert parameters.saturation_current.nominal.value == 1e-12
    assert parameters.ideality_factor.nominal.value == 1
    assert parameters.fixed_temperature.value == 300.15
    assert parameters.voltage_domain.minimum.value == -.05
    assert parameters.emission_voltage == pytest.approx(1.380649e-23 * 300.15 / 1.602176634e-19)
    assert tuple(map(str, parameters.evidence)) == (str(volt.content_hash(sample.EVIDENCE)),)
    del sample, model, element
    gc.collect()
    assert parameters.fixed_temperature.dimension == volt.UnitDimension.TEMPERATURE
    with pytest.raises(AttributeError):
        parameters.fixed_temperature = volt.Quantity(volt.UnitDimension.TEMPERATURE, 300)
    with pytest.raises(TypeError):
        volt.ShockleyDiodeElement("body", "a", "b", parameters)


@pytest.mark.parametrize("field,value", (("saturation_current", 0), ("saturation_current", -1),
    ("ideality_factor", 0), ("ideality_factor", -1), ("fixed_temperature", 0),
    ("fixed_temperature", -1), ("minimum", -.1), ("maximum", -.01)))
def test_diode_constructor_validation_is_native(field, value):
    arguments = dict(saturation_current=volt.ModelParameter(volt.Quantity(volt.UnitDimension.CURRENT, 1e-12)),
        ideality_factor=volt.ModelParameter(volt.Quantity(volt.UnitDimension.RATIO, 1)),
        fixed_temperature=volt.Quantity(volt.UnitDimension.TEMPERATURE, 300.15),
        voltage_domain=volt.QuantityRange.bounded(volt.Quantity(volt.UnitDimension.VOLTAGE, -.05),
                                                  volt.Quantity(volt.UnitDimension.VOLTAGE, .8)))
    if field in ("minimum", "maximum"):
        arguments["voltage_domain"] = volt.QuantityRange.bounded(
            volt.Quantity(volt.UnitDimension.VOLTAGE, value if field == "minimum" else -.05),
            volt.Quantity(volt.UnitDimension.VOLTAGE, value if field == "maximum" else .8))
    elif field == "fixed_temperature":
        arguments[field] = volt.Quantity(volt.UnitDimension.TEMPERATURE, value)
    else:
        arguments[field] = volt.ModelParameter(volt.Quantity(
            volt.UnitDimension.CURRENT if field == "saturation_current" else volt.UnitDimension.RATIO, value))
    with pytest.raises((ValueError, volt.InvalidArgumentError)):
        volt.DiodeParameters(**arguments)


def test_explicit_native_solution_has_independent_scalar_truth_and_budget_identity(authored_case):
    _sample_module, _result, _input, _request, model = authored_case
    original = model.to_json()
    linear = volt.solve_dc(model)
    assert not linear.success and linear.solution is None
    assert linear.outcome == volt.DcSolveOutcome.UNSUPPORTED_MODEL
    assert {item["code"] for item in linear.diagnostics} <= volt.ANALYSIS_DIAGNOSTIC_CODES
    options = volt.NonlinearDcSolveOptions()
    solved = volt.solve_dc(model, options)
    assert solved.success and solved.outcome == volt.DcSolveOutcome.CONVERGED
    probes = {str(item.key): item.value.value for item in solved.solution.probes}
    assert probes["anode-voltage"] == pytest.approx(.574476925589298085, abs=1e-9, rel=1e-9)
    assert probes["supply-current"] == pytest.approx(-.004425523074410701915, abs=1e-12, rel=1e-9)
    assert sum(branch.power.value for branch in solved.solution.branches) == pytest.approx(0, abs=1e-12)
    assert solved.metrics.correction_error_ratio <= 1
    assert solved.metrics.current_error_ratio <= 1
    assert solved.metrics.voltage_error_ratio <= 1
    assert solved.metrics.residual_evaluations <= options.max_residual_evaluations
    assert solved.metrics.jacobian_evaluations <= options.max_jacobian_evaluations
    assert solved.metrics.residual_weights
    assert model.to_json() == original
    changed = volt.solve_dc(model, volt.NonlinearDcSolveOptions(max_iterations=79))
    assert changed.success and changed.analysis_identity != solved.analysis_identity
    limited = volt.solve_dc(model, volt.NonlinearDcSolveOptions(max_iterations=1))
    assert not limited.success and limited.solution is None
    assert limited.outcome == volt.DcSolveOutcome.ITERATION_LIMIT
    assert {item["code"] for item in limited.diagnostics} <= volt.ANALYSIS_DIAGNOSTIC_CODES
    solution = solved.solution
    retained = solution.nonlinear_options
    del solved, model
    gc.collect()
    assert retained.max_iterations == 80
    assert volt.solve_dc(solution.model, retained).analysis_identity == solution.analysis_identity


@pytest.mark.parametrize("budget", ("max_iterations", "max_backtracks", "max_residual_evaluations", "max_jacobian_evaluations"))
def test_native_nonlinear_options_reject_invalid_budgets(budget):
    for value in (0, -1):
        with pytest.raises((ValueError, TypeError, volt.InvalidArgumentError)):
            volt.NonlinearDcSolveOptions(**{budget: value})


def _arguments(*extra):
    return _build_parser().parse_args(["simulate", "--design", "d", "--request", "r.json", "--output", "o", *extra])


@pytest.mark.parametrize("flags,code", ((["--method", "diode-newton", "--analysis", "ac"], "unsupported-diode-analysis"),
    (["--method", "diode-newton", "--backend", "ngspice"], "unsupported-diode-backend"),
    (["--max-iterations", "1"], "unexpected-diode-options"),
    (["--method", "diode-newton", "--max-iterations", "0"], "invalid-diode-options"),
    (["--method", "diode-newton", "--relative-tolerance", "nan"], "invalid-diode-options")))
def test_cli_numerical_options_reject_incompatible_and_invalid_requests(flags, code):
    with pytest.raises(CliError) as error:
        nonlinear_options(_arguments(*flags))
    assert error.value.code == code


def test_cli_tolerances_are_native_and_step_options_stay_transient():
    args = _arguments("--method", "diode-newton", "--absolute-current-tolerance", "1e-16")
    assert nonlinear_options(args).acceptance.absolute_current_tolerance.value == 1e-16
    assert transient_options(args) is None
    with pytest.raises(CliError):
        transient_options(_arguments("--method", "diode-newton", "--h-min", ".01"))


def _cli(arguments, cwd):
    return subprocess.run([sys.executable, "-m", "volt.cli", "simulate", *arguments, "--json"],
                          cwd=cwd, capture_output=True, text=True)


def test_source_and_poisoned_source_free_bundle_cli_parity_and_failures(authored_case, tmp_path):
    sample, result, input, request, model = authored_case
    unrelated = tmp_path / "unrelated"
    unrelated.mkdir()
    bundle = tmp_path / "diode.volt"
    result.write(bundle)
    request_path = tmp_path / "request.json"
    request_path.write_text(request.to_json())
    source = tmp_path / "source"
    shutil.copytree(Path(sample.__file__).parent, source, ignore=shutil.ignore_patterns("__pycache__", "build"))
    source_output = tmp_path / "source-result"
    common = ["--design", "diode-bias", "--request", str(request_path)]
    source_run = _cli(["--project", str(source), *common, "--method", "diode-newton", "--output", str(source_output)], unrelated)
    assert source_run.returncode == 0, source_run.stdout + source_run.stderr
    (source / "main.py").write_text("raise RuntimeError('poisoned source must not execute')\n")
    bundle_output = tmp_path / "bundle-result"
    bundle_run = _cli(["--bundle", str(bundle), *common, "--method", "diode-newton", "--output", str(bundle_output)], unrelated)
    assert bundle_run.returncode == 0, bundle_run.stdout + bundle_run.stderr
    native = volt.solve_dc(model, volt.NonlinearDcSolveOptions())
    for name in ("request.json", "compile-report.json", "solve-report.json", "solution.json"):
        assert (source_output / name).read_bytes() == (bundle_output / name).read_bytes()
    assert (bundle_output / "solve-report.json").read_text() == native.to_json()
    assert (bundle_output / "solution.json").read_text() == native.solution.to_json()
    for name, flags in (("linear-refused", []), ("budget", ["--method", "diode-newton", "--max-iterations", "1"])):
        output = tmp_path / name
        run = _cli(["--bundle", str(bundle), *common, "--output", str(output), *flags], unrelated)
        assert run.returncode == 1, run.stdout + run.stderr
        assert (output / "solve-report.json").is_file() and not (output / "solution.json").exists()
    bad = tmp_path / "bad"
    run = _cli(["--bundle", str(bundle), *common, "--output", str(bad), "--method", "diode-newton", "--max-iterations", "0"], unrelated)
    assert run.returncode == 2 and not bad.exists()
    collision = _cli(["--bundle", str(bundle), *common, "--output", str(bundle_output), "--method", "diode-newton"], unrelated)
    assert collision.returncode == 2
    assert (bundle_output / "solution.json").read_text() == native.solution.to_json()


def test_public_ac_transient_and_ngspice_refuse_diode_execution(authored_case):
    _sample_module, _result, input, _request, model = authored_case
    supply, _anode, ground = input.nets
    ac = volt.AcRequest("diode-ac", input, volt.AcFrequencySweep([volt.hertz(1000)]),
        reference=ground, sources=(volt.AcVoltageSource("drive", supply, ground,
        volt.Quantity(volt.UnitDimension.VOLTAGE, 1)),))
    transient = volt.TransientRequest("diode-transient", input,
        volt.TransientTimeGrid([volt.seconds(0), volt.seconds(.01)]), reference=ground,
        sources=(volt.TransientVoltageSource("drive", supply, ground,
            volt.TransientWaveform(volt.Quantity(volt.UnitDimension.VOLTAGE, 1))),))
    for request in (ac, transient):
        compiled = volt.compile_electrical(request)
        assert not compiled.complete and compiled.model is None
        assert compiled.diagnostics
        assert {item["code"] for item in compiled.diagnostics} <= volt.ANALYSIS_DIAGNOSTIC_CODES
    adapter = volt.prepare_ngspice_dc(model)
    assert not adapter.complete and adapter.diagnostics
    assert {item["code"] for item in adapter.diagnostics} <= volt.ANALYSIS_DIAGNOSTIC_CODES


def test_ngspice_diode_cli_retains_capability_report_without_deck_or_process(authored_case, tmp_path):
    _sample_module, result, _input, request, _model = authored_case
    bundle, request_path = tmp_path / "diode.volt", tmp_path / "request.json"
    result.write(bundle)
    request_path.write_text(request.to_json())
    output = tmp_path / "ngspice-refused"
    # A missing executable is never inspected because native lowering is unsupported.
    run = _cli(["--bundle", str(bundle), "--design", "diode-bias",
                "--request", str(request_path), "--output", str(output),
                "--backend", "ngspice", "--ngspice", str(tmp_path / "missing-ngspice")], tmp_path)
    assert run.returncode == 1, run.stdout + run.stderr
    payload = json.loads(run.stdout)
    assert payload["status"] == "incomplete"
    assert {path.name for path in output.iterdir()} == {
        "request.json", "compile-report.json", "ngspice-analysis.json"}
    assert payload["backend_report"]["diagnostics"]
    assert payload["process"] is None and payload["solve_report"] is None


def test_condition_evidence_requires_library_closure():
    library = volt.Library("test.diode.evidence", version="1")
    component = library.component("D", pins=(volt.PinSpec("A", 1), volt.PinSpec("B", 2)),
                                 contract=volt.ComponentContract("test.diode/D@1", ("A", "B")))
    builder = volt.PartElectricalModelBuilder(component)
    a, b = builder.terminal("a", "A"), builder.terminal("b", "B")
    source = _sample().DIODE.electrical_model.elements[0].parameters
    parameters = volt.DiodeParameters(source.saturation_current, source.ideality_factor,
        source.fixed_temperature, source.voltage_domain, evidence=(volt.content_hash(b"missing-condition-evidence"),))
    builder.add(volt.ShockleyDiodeElement, "body", a, b, parameters)
    library.part("D", component=component, electrical_model=builder.build(),
        footprint=volt.Footprint((library.namespace, "two"), pads=(
            volt.FootprintPad.surface_mount("1", at=(0, 0), size=(.5, .5)),
            volt.FootprintPad.surface_mount("2", at=(1, 0), size=(.5, .5)))),
        pads={"A": "1", "B": "2"}, manufacturer="Test", mpn="D", package="TEST")
    with pytest.raises((ValueError, RuntimeError), match="[Ee]vidence|asset"):
        library.build().bundle_bytes


def test_native_fixture_source_free_report_parity(tmp_path):
    native = tmp_path / "native"
    subprocess.run([os.environ["VOLT_NONLINEAR_PARITY_FIXTURE"], str(native)], check=True, capture_output=True)
    graph = volt.ProjectBundle.open(native / "project.volt").graph
    input = graph.loaded_project.circuits[0].electrical_input()
    request = volt.DcRequest.from_json(input, (native / "request.json").read_bytes())
    compiled = volt.compile_electrical(request)
    report = volt.solve_dc(compiled.model, volt.NonlinearDcSolveOptions())
    assert report.success
    assert report.to_json().encode() == (native / "report.json").read_bytes()
    assert report.solution.to_json().encode() == (native / "solution.json").read_bytes()

    failed_request = volt.DcRequest.from_json(input, (native / "failed-request.json").read_bytes())
    failed_model = volt.compile_electrical(failed_request).model
    failed = volt.solve_dc(failed_model, volt.NonlinearDcSolveOptions())
    assert not failed.success and failed.solution is None
    assert failed.to_json().encode() == (native / "failed-report.json").read_bytes()
    budget = volt.solve_dc(compiled.model, volt.NonlinearDcSolveOptions(max_iterations=1))
    assert budget.to_json().encode() == (native / "budget-report.json").read_bytes()
    unrelated = tmp_path / "unrelated"
    unrelated.mkdir()
    for name, request_name, flags, expected in (
        ("success", "request.json", [], "report.json"),
        ("domain", "failed-request.json", [], "failed-report.json"),
        ("budget", "request.json", ["--max-iterations", "1"], "budget-report.json")):
        output = tmp_path / name
        run = _cli(["--bundle", str(native / "project.volt"), "--design", "main",
                    "--request", str(native / request_name), "--output", str(output),
                    "--method", "diode-newton", *flags], unrelated)
        assert run.returncode == (0 if name == "success" else 1), run.stdout + run.stderr
        assert (output / "solve-report.json").read_bytes() == (native / expected).read_bytes()
        assert (output / "solution.json").exists() == (name == "success")
