"""Executable-free native ingestion, ownership and numerical trust regressions."""

import gc
import json
import os
import subprocess

import pytest

import volt


@pytest.fixture
def analysis(tmp_path):
    path = tmp_path / "native"
    subprocess.run(
        [os.environ["VOLT_DC_SOLVE_PARITY_FIXTURE"], str(path)],
        check=True, capture_output=True,
    )
    bundle = volt.ProjectBundle.open(path / "project.volt")
    dc_input = bundle.graph.loaded_project.circuits[0].electrical_input()
    request = volt.DcRequest.from_json(dc_input, (path / "request.json").read_bytes())
    return volt.prepare_ngspice_dc(volt.compile_electrical(request).model)


def _output(analysis, row="0 0 5 2.5 -0.0025"):
    headers = json.loads(analysis.to_json())["mapping"]["output"]["headers"]
    return (" ".join(headers) + "\n" + row + "\n").encode()


def test_external_results_retain_native_identity_orientation_and_ownership(analysis):
    native = volt.solve_dc(analysis.model)
    external = volt.solve_ngspice_dc(analysis, _output(analysis))
    assert external.success
    assert external.backend == "ngspice"
    assert external.analysis_identity != native.analysis_identity
    assert external.model.identity == native.model.identity
    assert external.provenance.backend_version == "46"
    assert external.provenance.validation_backend == native.backend
    assert external.provenance.deck_identity == analysis.deck_identity
    assert external.provenance.mapping_identity == analysis.mapping_identity
    payload = json.loads(external.to_json())
    assert payload["provenance"]["metrics_origin"] == "native_validation_of_external_observations"
    assert external.metrics.rank == external.metrics.coordinate_count
    assert external.metrics.scaled_residual <= external.options.relative_residual_tolerance
    for actual, expected in zip(external.solution.branches, native.solution.branches, strict=True):
        assert actual.branch == expected.branch
        assert actual.current.value == pytest.approx(expected.current.value, abs=1e-12)
        assert actual.power.value == pytest.approx(expected.power.value, abs=1e-12)
    solution = external.solution
    provenance = solution.provenance
    node, branch, probe = solution.nodes[1], solution.branches[2], solution.probes[0]
    expected_json = solution.to_json()
    del analysis, native, external
    gc.collect()
    assert solution.to_json() == expected_json
    assert node.potential.value == pytest.approx(2.5)
    assert branch.current.value == pytest.approx(-0.0025)
    assert probe.value.value == pytest.approx(2.5)
    assert provenance.backend == "ngspice"
    with pytest.raises(AttributeError):
        provenance.backend = "forged"
    for value in (volt.DcSolution, volt.DcSolveReport, volt.DcSolveProvenance, volt.NgspiceDcAnalysis):
        with pytest.raises(TypeError):
            value()


def test_external_observations_are_validated_not_replaced_by_native_answers(analysis):
    # This perturbation is inside the declared residual policy and must be preserved.
    accepted = volt.solve_ngspice_dc(analysis, _output(analysis, "0 0 5 2.5000000001 -0.0025"))
    assert accepted.success
    assert accepted.solution.nodes[1].potential.value == 2.5000000001
    rejected = volt.solve_ngspice_dc(analysis, _output(analysis, "0 0 5 3 -0.0025"))
    assert not rejected.success
    assert rejected.outcome == volt.DcSolveOutcome.RESIDUAL_FAILURE
    assert rejected.solution is None
    assert rejected.metrics.current_error_ratio > 1


def test_external_effective_acceptance_settings_are_identified(analysis):
    first = volt.solve_ngspice_dc(analysis, _output(analysis))
    changed = volt.solve_ngspice_dc(
        analysis, _output(analysis), volt.DcSolveOptions(relative_residual_tolerance=2e-9)
    )
    assert changed.success
    assert changed.analysis_identity != first.analysis_identity
    assert changed.provenance.deck_identity == first.provenance.deck_identity


@pytest.mark.parametrize("row", ["", "0 0 5 2.5", "0 0 5 2.5 nan", "0 0 5 2.5 inf", "0 0 5 2.5 -0.0025 8"])
def test_external_integrity_errors_raise_without_a_result(analysis, row):
    with pytest.raises((ValueError, RuntimeError)):
        volt.solve_ngspice_dc(analysis, _output(analysis, row))


def test_external_mapping_association_is_checked(analysis):
    content = _output(analysis).replace(b"volt_mapping_", b"wrong_mapping_", 1)
    with pytest.raises((ValueError, RuntimeError)):
        volt.solve_ngspice_dc(analysis, content)
    with pytest.raises(TypeError):
        volt.solve_ngspice_dc(analysis.model, _output(analysis))
