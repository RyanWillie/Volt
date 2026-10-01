import gc
import json
import os
import subprocess

import pytest

import volt


@pytest.fixture
def native_case(tmp_path):
    path = tmp_path / "native"
    subprocess.run([os.environ["VOLT_DC_SOLVE_PARITY_FIXTURE"], str(path)],
                   check=True, capture_output=True)
    bundle = volt.ProjectBundle.open(path / "project.volt")
    input = bundle.graph.loaded_project.circuits[0].electrical_input()
    request = volt.DcRequest.from_json(input, (path / "request.json").read_bytes())
    return volt.compile_electrical(request).model, path


def test_native_python_report_and_solution_parity(native_case):
    model, path = native_case
    original = model.to_json()
    report = volt.solve_dc(model)
    assert report.success
    assert report.outcome == volt.DcSolveOutcome.SUCCESS
    assert report.to_json().encode() == (path / "report.json").read_bytes()
    assert report.solution.to_json().encode() == (path / "solution.json").read_bytes()
    assert model.to_json() == original
    assert report.metrics.rank == report.metrics.coordinate_count
    assert report.metrics.reciprocal_condition >= report.options.minimum_reciprocal_condition
    assert report.metrics.scaled_residual <= report.options.relative_residual_tolerance
    assert report.metrics.voltage_error_ratio <= 1
    assert report.metrics.current_error_ratio <= 1
    solution = report.solution
    assert [item.node for item in solution.nodes] == [0, 1, 2]
    assert [item.potential.value for item in solution.nodes] == pytest.approx([5, 2.5, 0])
    assert [item.current.value for item in solution.branches] == pytest.approx([.0025, .0025, -.0025])
    assert sum(item.power.value for item in solution.branches) == pytest.approx(0, abs=1e-12)
    assert solution.probes[0].key.value == "midpoint-voltage"
    assert solution.probes[0].value.value == pytest.approx(2.5)
    assert solution.probes[0].value.dimension == volt.UnitDimension.VOLTAGE
    assert all(item.power.dimension == volt.UnitDimension.POWER for item in solution.branches)


def test_solution_and_observations_own_their_lifetimes(native_case):
    model, _ = native_case
    report = volt.solve_dc(model)
    solution = report.solution
    node, branch, probe = solution.nodes[1], solution.branches[2], solution.probes[0]
    identity = solution.analysis_identity
    options = solution.options
    expected = solution.to_json()
    del report, model, native_case
    gc.collect()
    assert solution.to_json() == expected
    assert volt.solve_dc(solution.model, options).analysis_identity == identity
    del solution
    gc.collect()
    assert node.potential.value == pytest.approx(2.5)
    assert branch.current.value == pytest.approx(-.0025)
    assert probe.value.value == pytest.approx(2.5)
    with pytest.raises(AttributeError):
        branch.current = volt.Quantity(volt.UnitDimension.CURRENT, 1)
    with pytest.raises(TypeError):
        volt.DcSolution()


def test_options_are_native_validated_and_identified(native_case):
    model, _ = native_case
    defaults = volt.DcSolveOptions()
    options = volt.DcSolveOptions(relative_residual_tolerance=2e-9)
    report = volt.solve_dc(model, options)
    assert report.success
    assert report.analysis_identity != volt.solve_dc(model).analysis_identity
    assert report.options.relative_residual_tolerance == 2e-9
    assert defaults.scaling == "max_abs_row_then_column"
    for name in ("relative_rank_threshold", "minimum_reciprocal_condition", "relative_residual_tolerance"):
        for value in (0, -1, 1, float("nan"), float("inf")):
            with pytest.raises((ValueError, RuntimeError)):
                volt.DcSolveOptions(**{name: value})
    for name, dimension in (("absolute_voltage_tolerance", volt.UnitDimension.CURRENT),
                            ("absolute_current_tolerance", volt.UnitDimension.VOLTAGE)):
        with pytest.raises((ValueError, RuntimeError)):
            volt.DcSolveOptions(**{name: volt.Quantity(dimension, 1e-9)})
    with pytest.raises(AttributeError):
        options.relative_residual_tolerance = 1e-3


def test_rank_failure_is_typed_and_never_exposes_a_solution():
    design = volt.Design("floating")
    design.net("reference")
    design.net("unfixed")
    input = volt.prepare_electrical_input(design)
    compiled = volt.compile_electrical(volt.DcRequest("floating", input, reference=input.nets[0]))
    assert compiled.complete
    report = volt.solve_dc(compiled.model)
    assert not report.success
    assert report.outcome == volt.DcSolveOutcome.RANK_DEFICIENT
    assert report.solution is None
    assert report.metrics.rank < report.metrics.coordinate_count
    assert report.diagnostics
    assert json.loads(report.to_json())["solution"] is None
    assert report.model.identity == compiled.model.identity
    assert {item["code"] for item in report.diagnostics} <= volt.ANALYSIS_DIAGNOSTIC_CODES


def test_incomplete_compilation_and_python_dicts_cannot_enter_solver():
    design = volt.Design("missing-reference")
    input = volt.prepare_electrical_input(design)
    report = volt.compile_electrical(volt.DcRequest("incomplete", input))
    assert not report.complete
    for value in (report, report.model, {"matrix": [[1]], "rhs": [5]}):
        with pytest.raises(TypeError):
            volt.solve_dc(value)


def test_ordinary_build_remains_solver_free():
    project = volt.Project("ordinary")

    @project.design
    def design():
        return volt.Design("ordinary")

    assert project.run().ok
