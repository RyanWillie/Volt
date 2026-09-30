import math
import importlib.util
from pathlib import Path
import json
import pytest
import volt
from test_cli_simulate import linear_project, _run, _payload, _circuit, _assert_published


def _request(input, phase=0.0):
    supply, midpoint, reference = input.nets
    return volt.AcRequest(
        "divider-ac", input, volt.AcFrequencySweep([volt.hertz(10), volt.hertz(1000)]),
        reference=reference,
        sources=[volt.AcVoltageSource("drive", supply, reference,
                                      volt.Quantity(volt.UnitDimension.VOLTAGE, 1), phase)],
        probes=[volt.AcVoltageProbe("out", midpoint, reference),
                volt.AcVoltageProbe("in", supply, reference)],
        gains=[volt.AcGainProbe("gain", "out", "in")],
    )


@pytest.mark.parametrize("values", [[], [0], [-1], [1, 1], [2, 1], [math.inf], [math.nan]])
def test_frequency_boundary_rejections(values):
    with pytest.raises((volt.InvalidArgumentError, ValueError)):
        volt.AcFrequencySweep([volt.hertz(value) for value in values])


def test_native_sweep_endpoints_and_zero_phase():
    for constructor in (volt.AcFrequencySweep.linear, volt.AcFrequencySweep.logarithmic):
        sweep = constructor(volt.hertz(1), volt.hertz(1000), 7)
        assert sweep.frequencies[0].value == 1
        assert sweep.frequencies[-1].value == 1000
        assert all(a.value < b.value for a, b in zip(sweep.frequencies, sweep.frequencies[1:]))
    zero = volt.AcComplexQuantity(volt.UnitDimension.VOLTAGE, 0, 0)
    assert zero.phase is None


def test_native_binding_request_codec_and_complex_orientation(linear_project):
    _, bundle = linear_project
    input = _circuit(bundle, "divider").dc_input()
    request = _request(input, math.pi / 2)
    assert request.sources[0].phasor == pytest.approx(1j)
    assert volt.AcRequest.from_json(input, request.to_json()).to_json() == request.to_json()
    compiled = volt.compile_electrical(request)
    assert compiled.complete
    assert compiled.model.ac_request.to_json() == request.to_json()
    report = volt.solve_ac(compiled.model)
    assert report.success
    assert len(report.points) == 2
    for point in report.solution.points:
        probes = {probe.key.value: probe.value for probe in point.probes}
        assert probes["out"].value == pytest.approx(0.5j)
        assert probes["out"].phase == pytest.approx(math.pi / 2)
        assert probes["gain"].value == pytest.approx(0.5)
    assert json.loads(report.solution.to_json())


def test_ac_source_and_offline_bundle_parity(linear_project, tmp_path):
    project, bundle = linear_project
    request_path = tmp_path / "request.json"
    request_path.write_text(_request(_circuit(bundle, "divider").dc_input()).to_json())
    outputs = []
    for selector, path in (("--project", project), ("--bundle", bundle)):
        if selector == "--bundle":
            (project / "project_entry.py").write_text("raise RuntimeError('offline AC imported source')\n")
            (project / "volt.toml").write_text("invalid TOML")
        output = tmp_path / selector.removeprefix("--")
        run = _run(tmp_path, "simulate", selector, str(path), "--design", "divider",
                   "--analysis", "ac", "--backend", "native", "--request", str(request_path),
                   "--output", str(output), "--json")
        assert run.returncode == 0, run
        payload = _payload(run)
        _assert_published(payload, output,
                          {"request.json", "compile-report.json", "solve-report.json", "solution.json"})
        assert payload["solve_report"]["provenance"]["adapter"] == "volt.native-linear-ac"
        assert payload["solve_report"]["provenance"]["backend"]
        assert payload["solve_report"]["provenance"]["backend_version"]
        assert payload["solve_report"]["analysis_identity"]
        outputs.append(output)
    for name in ("request.json", "compile-report.json", "solve-report.json", "solution.json"):
        assert (outputs[0] / name).read_bytes() == (outputs[1] / name).read_bytes()


def test_ngspice_ac_rejected_before_source_execution(tmp_path):
    run = _run(tmp_path, "simulate", "--analysis", "ac", "--backend", "ngspice",
               "--project", str(tmp_path / "missing"), "--design", "divider",
               "--request", str(tmp_path / "missing.json"), "--output", str(tmp_path / "out"), "--json")
    assert run.returncode == 2
    assert "unsupported-ac-backend" in run.stdout
    assert not (tmp_path / "out").exists()


@pytest.mark.parametrize("kind", ["incomplete", "floating", "undefined"])
def test_ac_cli_failed_or_incomplete_analysis_retains_evidence(linear_project, tmp_path, kind):
    _, bundle = linear_project
    input = _circuit(bundle, "floating" if kind == "floating" else "divider").dc_input()
    sweep = volt.AcFrequencySweep([volt.hertz(100)])
    if kind == "incomplete":
        request = volt.AcRequest("incomplete", input, sweep)
    elif kind == "floating":
        request = volt.AcRequest("floating", input, sweep, reference=input.nets[0],
                                sources=[volt.AcCurrentSource("drive", input.nets[1], input.nets[0],
                                         volt.Quantity(volt.UnitDimension.CURRENT, 1))])
    else:
        supply, midpoint, reference = input.nets
        request = volt.AcRequest("undefined", input, sweep, reference=reference,
                                sources=[volt.AcVoltageSource("drive", supply, reference,
                                         volt.Quantity(volt.UnitDimension.VOLTAGE, 1))],
                                probes=[volt.AcVoltageProbe("out", midpoint, reference),
                                        volt.AcVoltageProbe("zero", reference, reference)],
                                gains=[volt.AcGainProbe("undefined", "out", "zero")])
    request_path = tmp_path / f"{kind}.json"
    request_path.write_text(request.to_json())
    output = tmp_path / f"{kind}-results"
    run = _run(tmp_path, "simulate", "--bundle", str(bundle), "--design",
               "floating" if kind == "floating" else "divider", "--analysis", "ac",
               "--request", str(request_path), "--output", str(output), "--json")
    assert run.returncode == 1, run
    payload = _payload(run)
    assert payload["status"] == ("incomplete" if kind == "incomplete" else "failed")
    assert (output / "request.json").exists()
    assert not (output / "solution.json").exists()
    if kind != "incomplete":
        assert (output / "solve-report.json").exists()
        assert payload["solve_report"]["points"]


def test_sdk_rc_example_gain_at_cutoff(tmp_path):
    sample = Path(__file__).parents[2] / "samples" / "linear_ac" / "main.py"
    spec = importlib.util.spec_from_file_location("volt_sample_linear_ac", sample)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    project = module.main()
    design = project.run_through(project.design).design("lowpass")
    nets = {net.name: net for net in design.nets()}
    input = volt.prepare_ac_input(design)
    supply, output, reference = (input.net(nets[name]) for name in ("INPUT", "OUTPUT", "GROUND"))
    request = volt.AcRequest(
        "rc-cutoff", input, volt.AcFrequencySweep([volt.hertz(1 / (2 * math.pi * 1000 * 1e-6))]),
        reference=reference,
        sources=[volt.AcVoltageSource("drive", supply, reference, volt.Quantity(volt.UnitDimension.VOLTAGE, 1))],
        probes=[volt.AcVoltageProbe("out", output, reference), volt.AcVoltageProbe("in", supply, reference)],
        gains=[volt.AcGainProbe("gain", "out", "in")],
    )
    report = volt.solve_ac(volt.compile_electrical(request).model)
    assert report.success
    gain = next(probe.value for probe in report.solution.points[0].probes if probe.key.value == "gain")
    assert gain.value == pytest.approx(0.5 - 0.5j, abs=1e-9, rel=1e-7)
    assert gain.magnitude == pytest.approx(math.sqrt(0.5), rel=1e-7)
    assert gain.phase == pytest.approx(-math.pi / 4, abs=1e-5)


def test_native_keyed_impedance_uses_current_entering_positive_port(linear_project):
    _, bundle = linear_project
    input = _circuit(bundle, "divider").dc_input()
    supply, _, reference = input.nets
    request = volt.AcRequest(
        "input-impedance", input, volt.AcFrequencySweep([volt.hertz(100)]), reference=reference,
        sources=[volt.AcCurrentSource("test", supply, reference,
                                      volt.Quantity(volt.UnitDimension.CURRENT, 1e-3), math.pi / 2)],
        probes=[volt.AcVoltageProbe("port-voltage", supply, reference)],
        impedances=[volt.AcImpedanceProbe("impedance", supply, reference, "test")],
    )
    assert volt.AcRequest.from_json(input, request.to_json()).to_json() == request.to_json()
    report = volt.solve_ac(volt.compile_electrical(request).model)
    assert report.success
    probes = {probe.key.value: probe.value for probe in report.solution.points[0].probes}
    assert probes["port-voltage"].value == pytest.approx(-2j)
    assert probes["impedance"].value == pytest.approx(2000, abs=1e-6, rel=1e-7)
    assert probes["impedance"].dimension == volt.UnitDimension.RESISTANCE
