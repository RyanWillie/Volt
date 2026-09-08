import gc
import json
import os
import subprocess

import pytest

import volt


@pytest.fixture
def native_compilation(tmp_path):
    path = tmp_path / "native"
    subprocess.run(
        [os.environ["VOLT_ELECTRICAL_COMPILATION_PARITY_FIXTURE"], str(path)],
        check=True, capture_output=True,
    )
    bundle = volt.ProjectBundle.open(path / "project.volt")
    input = bundle.graph.loaded_project.circuits[0].dc_input()
    return input, path


def test_python_authored_compilation_matches_native_snapshot(native_compilation):
    input, path = native_compilation
    supply, midpoint, reference = input.nets
    request = volt.DcRequest(
        "divider-operating-point", input, reference=reference,
        sources=[volt.DcVoltageSource(
            "supply-5v", supply, reference,
            volt.Quantity(volt.UnitDimension.VOLTAGE, 5),
        )],
        probes=[volt.DcVoltageProbe("midpoint-voltage", midpoint, reference)],
    )
    assert request.to_json().encode() == (path / "request.json").read_bytes()
    report = volt.compile_electrical(request)
    assert report.complete
    assert report.input == input.identity
    assert report.to_json().encode() == (path / "report.json").read_bytes()
    assert report.model.to_json().encode() == (path / "compiled.json").read_bytes()
    assert all(item.status == volt.DcCoverageStatus.SUPPORTED for item in report.coverage)
    document = json.loads(report.model.to_json())
    assert len(document["nodes"]) == 3
    assert len(document["branches"]) == 3
    assert document["identity"] == report.model.identity.value
    assert document["request_identity"] == report.model.request_identity.value
    assert "solution" not in document


def test_reopened_request_compiles_and_model_owns_lifetime(native_compilation):
    input, path = native_compilation
    request = volt.DcRequest.from_json(input, (path / "request.json").read_bytes())
    report = volt.compile_electrical(request)
    model = report.model
    expected = model.to_json()
    del input, request, report, native_compilation
    gc.collect()
    assert model.to_json() == expected
    assert volt.compile_electrical(model.request).model.to_json() == expected
    with pytest.raises(AttributeError):
        model.identity = "changed"
    with pytest.raises(TypeError):
        volt.CompiledElectricalModel()


def test_missing_reference_has_diagnostics_but_no_partial_model(native_compilation):
    input, _ = native_compilation
    report = volt.compile_electrical(volt.DcRequest("no-reference", input))
    assert not report.complete
    assert report.model is None
    assert len(report.coverage) == 2
    assert "DC_REQUEST_REFERENCE_MISSING" in {item["code"] for item in report.diagnostics}
    document = json.loads(report.to_json())
    assert document["model"] is None
    assert not document["complete"]


def test_compilation_rejects_python_graph_and_keeps_ordinary_build_unchanged():
    with pytest.raises(TypeError):
        volt.compile_electrical({"nodes": [], "branches": []})
    project = volt.Project("ordinary-without-analysis")

    @project.design
    def design():
        return volt.Design("ordinary-without-analysis")

    assert project.run().ok
