import gc
import json
import os
import subprocess

import pytest

import volt


def _volts(value):
    return volt.Quantity(volt.UnitDimension.VOLTAGE, value)


def _amps(value):
    return volt.Quantity(volt.UnitDimension.CURRENT, value)


@pytest.fixture
def native_input(tmp_path):
    path = tmp_path / "native"
    subprocess.run(
        [os.environ["VOLT_DC_REQUEST_PARITY_FIXTURE"], str(path)],
        check=True, capture_output=True,
    )
    bundle = volt.ProjectBundle.open(path / "project.volt")
    input = bundle.graph.loaded_project.circuits[0].dc_input()
    return input, (path / "request.json").read_bytes()


def _complete(input):
    positive, negative = input.nets
    return volt.DcRequest(
        "operating-point", input, reference=negative,
        sources=[
            volt.DcCurrentSource("load", positive, negative, _amps(0.01)),
            volt.DcVoltageSource("drive", positive, negative, _volts(5)),
        ],
        probes=[
            volt.DcSourceCurrentProbe("supply-current", "drive"),
            volt.DcModelElementCurrentProbe("resistor-current", input.occurrences[0], "esr"),
            volt.DcVoltageProbe("rail", positive, negative),
        ],
        exclusions=[
            volt.DcOccurrenceExclusion(input.occurrences[3], volt.DcNonElectricalExclusion()),
            volt.DcOccurrenceExclusion(input.occurrences[2], volt.DcOutsideAnalysisExclusion()),
            volt.DcOccurrenceExclusion(
                input.occurrences[1], volt.DcReplacedByStimulusExclusion(["drive"])
            ),
        ],
    )


def test_python_authoring_matches_native_bytes_and_reopens(native_input):
    input, expected = native_input
    request = _complete(input)
    assert request.to_json().encode() == expected
    for data in (expected, expected.decode()):
        reopened = volt.DcRequest.from_json(input, data)
        assert reopened.to_json().encode() == expected
        assert reopened.assess().complete
    assessment = volt.assess_dc_request(request)
    assert assessment.input == input.identity
    assert assessment.diagnostics == []
    assert [entry.status for entry in assessment.coverage] == [
        volt.DcCoverageStatus.SUPPORTED,
        volt.DcCoverageStatus.EXCLUDED,
        volt.DcCoverageStatus.EXCLUDED,
        volt.DcCoverageStatus.EXCLUDED,
    ]
    assert assessment.coverage[0].selected_part["part_key"] == "modeled"
    assert assessment.coverage[1].exclusion.reason.sources[0].value == "drive"


def test_sourcefree_request_and_refs_own_their_native_input(native_input):
    input, expected = native_input
    request = _complete(input)
    positive = input.nets[0]
    del input, native_input
    gc.collect()
    assert request.to_json().encode() == expected
    assert request.assess().complete
    assert positive == request.input.nets[0]
    with pytest.raises(AttributeError):
        request.reference = positive


def test_missing_reference_and_coverage_are_native_diagnostics(native_input):
    input, _ = native_input
    report = volt.DcRequest("incomplete", input).assess()
    assert not report.complete
    assert [entry.status for entry in report.coverage] == [
        volt.DcCoverageStatus.SUPPORTED,
        volt.DcCoverageStatus.MODEL_ABSENT,
        volt.DcCoverageStatus.UNSELECTED,
        volt.DcCoverageStatus.UNSELECTED,
    ]
    codes = {item["code"] for item in report.diagnostics}
    assert "DC_REQUEST_REFERENCE_MISSING" in codes
    assert codes <= volt.ANALYSIS_DIAGNOSTIC_CODES


def test_request_rejects_bad_native_relationships(native_input):
    input, _ = native_input
    positive, negative = input.nets
    source = volt.DcVoltageSource("drive", positive, negative, _volts(5))
    invalid = [
        dict(sources=[source, source]),
        dict(probes=[volt.DcSourceCurrentProbe("p", "missing")]),
        dict(probes=[volt.DcModelElementCurrentProbe("p", input.occurrences[0], "missing")]),
        dict(exclusions=[volt.DcOccurrenceExclusion(
            input.occurrences[1], volt.DcReplacedByStimulusExclusion(["missing"])
        )]),
    ]
    for arguments in invalid:
        with pytest.raises((RuntimeError, ValueError, IndexError)):
            volt.DcRequest("invalid", input, **arguments)
    with pytest.raises(ValueError):
        volt.DcVoltageSource("bad", positive, negative, _amps(1))
    with pytest.raises(ValueError):
        volt.DcCurrentSource("bad", positive, positive, _amps(1))
    with pytest.raises(TypeError):
        volt.DcRequest("bad", input, sources=[{"voltage": 5}])


def test_foreign_and_stale_authoring_handles_are_rejected():
    design = volt.Design("original")
    positive = design.net("positive")
    negative = design.net("negative")
    input = volt.prepare_dc_input(design)
    assert input.net(positive) == input.nets[0]
    other = volt.Design("other")
    foreign = other.net("positive")
    with pytest.raises(RuntimeError):
        input.net(foreign)
    design.net("new")
    with pytest.raises(RuntimeError):
        input.net(negative)
    fresh = volt.prepare_dc_input(design)
    with pytest.raises(RuntimeError):
        volt.DcRequest("foreign", input, reference=fresh.nets[0])
    # The captured snapshot remains usable after authoring changes.
    assert volt.DcRequest("snapshot", input, reference=input.nets[1]).assess().complete


def test_current_only_transport_rejects_changed_identity_and_shape(native_input):
    input, expected = native_input
    for field, value in (("version", 0), ("unexpected", True)):
        document = json.loads(expected)
        document[field] = value
        with pytest.raises((RuntimeError, ValueError)):
            volt.DcRequest.from_json(input, json.dumps(document))
    foreign = volt.prepare_dc_input(volt.Design("foreign"))
    with pytest.raises(RuntimeError):
        volt.DcRequest.from_json(foreign, expected)


def test_ordinary_project_does_not_require_a_dc_request():
    project = volt.Project("ordinary")

    @project.design
    def design():
        return volt.Design("ordinary")

    assert project.run().ok
