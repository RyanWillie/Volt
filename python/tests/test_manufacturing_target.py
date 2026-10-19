"""Full native source-free manufacturing coverage parity; all profiles are synthetic."""
import json
import os
import subprocess
import sys

import pytest
import volt


def fixture(mode="a"):
    return json.loads(subprocess.check_output(
        [os.environ["VOLT_MANUFACTURING_PARITY_FIXTURE"], mode], text=True,
    ))


def row(report, key):
    return next(item for item in report["coverage"] if item["check"] == key)


@pytest.mark.parametrize("mode", ["a", "b", "missing", "short"])
def test_entire_native_payload_and_bytes_match_without_source(mode, tmp_path):
    native = fixture(mode)
    artifact = bytes.fromhex(native["artifact_hex"])
    target = volt.read_manufacturing_target(native["target"])
    assert isinstance(target, volt.ManufacturingTarget)
    result = volt.evaluate_manufacturing_coverage(artifact, target)
    assert isinstance(result, volt.ManufacturingCoverage)
    assert result.to_json() == native["report"]
    assert not result.ready
    assert result.status == volt.ManufacturingStatus.Blocked
    report = json.loads(result.to_json())
    assert report["counts"]["required"] == len(report["coverage"]) == 65
    assert all(item["required"] for item in report["coverage"])
    assert sum(report["counts"][name] for name in ("native_pass", "failed", "not_evaluated", "not_applicable")) == 65
    for item in report["coverage"]:
        assert item["entity_count"] == len(item["entities"])
        assert item["observation_count"] == len(item["observations"])
        assert "profile_digest" not in item
        for observation in item["observations"]:
            assert observation["entity_count"] == len(observation["entities"])
    assert row(report, "SelectedPartRatings")["state"] == "not-evaluated"
    assert row(report, "ExportFidelity")["state"] == "not-evaluated"
    assert row(report, "MaskWeb")["state"] == "not-evaluated"
    assert row(report, "RequiredPolarity")["state"] == "not-evaluated"
    assert "diagnostics" not in report
    assert report["profile_digest"] == result.profile_digest
    assert isinstance(report["completion"]["diagnostics"], list)
    assert target.digest == result.target_digest
    assert volt.write_manufacturing_target(target) == native["target"]
    artifact_path = tmp_path / "isolated.board"
    target_path = tmp_path / "target.json"
    artifact_path.write_bytes(artifact)
    target_path.write_text(native["target"])
    del artifact, target, result
    reopened = volt.evaluate_manufacturing_coverage(
        artifact_path.read_bytes(), volt.read_manufacturing_target(target_path.read_text()),
    )
    assert reopened.to_json() == native["report"]
    if mode in ("a", "b"):
        assert row(report, "ArtifactIdentity")["state"] == "native-pass"
        assert row(report, "CopperWidth")["state"] == ("native-pass" if mode == "a" else "failed")
    if mode == "missing":
        assert row(report, "RequiredInventory")["state"] == "failed"
    if mode == "short":
        assert row(report, "UnintendedContacts")["state"] == "failed"


def test_native_typed_construction_matches_codec_and_is_owned():
    native = fixture()
    artifact = bytes.fromhex(native["artifact_hex"])
    decoded = volt.read_manufacturing_target(native["target"])
    source = json.loads(native["target"])
    process = decoded.process
    profile = volt.ManufacturingProfile(
        decoded.profile.capability_json, decoded.profile.source_content, process,
        volt.ManufacturingRange(10, 40), volt.ManufacturingRange(10, 30), 0.15,
    )
    constructed = volt.make_manufacturing_target(
        artifact, volt.ManufacturingGoal.Fabrication, process, profile,
    )
    assert constructed.digest == decoded.digest
    explicit = volt.ManufacturingTarget(
        source["source"]["board"], source["source"]["provenance_digest"], source["source"]["content_digest"],
        constructed.goal, process, profile,
    )
    assert explicit.digest == constructed.digest
    process.thickness_mm = 1.7
    assert constructed.process.thickness_mm == 1.6
    assert decoded.digest == constructed.digest
    with pytest.raises(AttributeError):
        constructed.goal = volt.ManufacturingGoal.FabricationAndAssembly
    result = volt.evaluate_manufacturing_coverage(artifact, constructed)
    with pytest.raises(AttributeError):
        result.ready = True


@pytest.mark.parametrize("field", ["name", "source", "as_of", "minimum_track_width_mm", "minimum_via_drill_mm",
    "minimum_via_annular_mm", "minimum_clearances", "copper_weight_refinements", "supported_copper_layer_counts",
    "board_thickness_range_mm", "available_copper_weights_oz", "drill_diameter_range_mm"])
def test_every_capability_profile_field_participates_in_identity(field):
    native = fixture()
    doc = json.loads(native["target"])
    capability = doc["profile"]["capability"]["profile"]
    if field == "name":
        capability[field] += " changed"
    elif field in ("source", "as_of"):
        capability["provenance"][field] += " changed"
    elif field.startswith("minimum_") and field != "minimum_clearances":
        capability[field] += 0.01
    elif field == "minimum_clearances":
        capability[field] = [{"first": "track", "second": "pad", "clearance_mm": 0.3}]
    elif field == "copper_weight_refinements":
        capability[field] = [{"copper_weight_oz": 1, "minimum_track_width_mm": 0.2, "minimum_clearance_mm": 0.2}]
    elif field == "supported_copper_layer_counts":
        capability[field] = [2, 4]
    elif field == "available_copper_weights_oz":
        capability[field] = [1]
    else:
        capability[field]["maximum_mm"] += 0.1
    target = volt.read_manufacturing_target(json.dumps(doc))
    original = volt.read_manufacturing_target(native["target"])
    assert target.digest != original.digest
    report = json.loads(volt.evaluate_manufacturing_coverage(bytes.fromhex(native["artifact_hex"]), target).to_json())
    assert row(report, "ArtifactIdentity")["state"] == "failed"
    assert row(report, "ArtifactIdentity")["code"] == "MANUFACTURING_IDENTITY_MISMATCH"
    assert row(report, "CopperWidth")["state"] == "not-evaluated"


@pytest.mark.parametrize("field", ["dimension_x_mm", "dimension_y_mm", "minimum_via_radial_annulus_mm"])
def test_missing_additional_limits_never_pass(field):
    native = fixture()
    doc = json.loads(native["target"])
    doc["profile"][field] = None
    target = volt.read_manufacturing_target(json.dumps(doc))
    report = json.loads(volt.evaluate_manufacturing_coverage(bytes.fromhex(native["artifact_hex"]), target).to_json())
    key = {"dimension_x_mm": "DimensionX", "dimension_y_mm": "DimensionY", "minimum_via_radial_annulus_mm": "ViaRadialAnnulus"}[field]
    assert row(report, key)["state"] == "not-evaluated"
    assert row(report, key)["code"] == "MANUFACTURING_MISSING_INPUT"
    assert not report["ready"]


@pytest.mark.parametrize("field", ["schema_version", "policy_version"])
def test_typed_unsupported_version_errors(field):
    doc = json.loads(fixture()["target"])
    doc[field] = 2
    with pytest.raises(volt.ManufacturingTargetReadError) as failure:
        volt.read_manufacturing_target(json.dumps(doc))
    assert failure.value.code == "UnsupportedVersion"


@pytest.mark.parametrize("text", ["{broken", "{}", "[]", '{"format": "x", "format": "y"}'])
def test_typed_malformed_errors(text):
    with pytest.raises(volt.ManufacturingTargetReadError) as failure:
        volt.read_manufacturing_target(text)
    assert failure.value.code == "Malformed"


def test_target_profile_provenance_and_explicit_cpl_change_identity():
    native = fixture()
    original = volt.read_manufacturing_target(native["target"])
    doc = json.loads(native["target"])
    doc["profile"]["source_content"] = "sha256:" + "0" * 64
    assert volt.read_manufacturing_target(json.dumps(doc)).digest != original.digest
    doc = json.loads(native["target"])
    doc["goal"] = "FabricationAndAssembly"
    doc["process"]["assembly"] = doc["profile"]["applicability"]["assembly"] = "SmtAndThroughHole"
    doc["cpl_options"] = {"rotation_offsets": []}
    empty = volt.read_manufacturing_target(json.dumps(doc))
    options = volt.ManufacturingCplOptions()
    options.rotation_offsets = [volt.ManufacturingCplOffset("test", "contact", 90)]
    changed_target = volt.make_manufacturing_target(bytes.fromhex(native["artifact_hex"]), empty.goal,
        empty.process, empty.profile, options)
    assert changed_target.digest != empty.digest
    report = json.loads(volt.evaluate_manufacturing_coverage(bytes.fromhex(native["artifact_hex"]), changed_target).to_json())
    assert row(report, "AssemblyOrientation")["state"] == "not-evaluated"
    assert row(report, "CplRotation")["state"] == "native-pass"
    assert row(report, "SelectedPartRatings")["state"] == "not-evaluated"


def test_exception_translator_owns_type_after_module_attribute_deletion():
    subprocess.run([sys.executable, "-c", """
import gc
import weakref
import volt
error_type = weakref.ref(volt.ManufacturingTargetReadError)
del volt.ManufacturingTargetReadError
del volt._volt.ManufacturingTargetReadError
gc.collect()
assert error_type() is not None
try:
    volt.read_manufacturing_target("{}")
except Exception as error:
    assert type(error) is error_type()
    assert error.code == "Malformed"
else:
    raise AssertionError("Malformed target accepted")
"""], check=True)


@pytest.mark.parametrize("mode", ["assembly", "rating-accepted", "rating-current", "rating-budget", "rating-failure", "rating-unknown", "rating-conditional"])
def test_exact_assembly_and_rating_native_evidence_has_full_source_free_parity(mode, tmp_path):
    native = fixture(mode)
    artifact = bytes.fromhex(native["artifact_hex"])
    target = volt.read_manufacturing_target(native["target"])
    before = artifact[:]
    result = volt.evaluate_manufacturing_coverage(artifact, target)
    assert result.to_json() == native["report"]
    report = json.loads(result.to_json())
    assert report["schema_version"] == 2
    assert report["policy_version"] == 1
    assert report["evaluator"] == "volt.manufacturing-coverage/2"
    assert len(report["coverage"]) == 65
    assert report["counts"]["required"] == 65
    assert not report["ready"]
    for key in ("PopulatedQuantities", "DnpQuantities", "SelectedAlternates", "CplIdentity", "CplSide", "CplRotation"):
        assert row(report, key)["state"] == "native-pass"
    expected = "failed" if mode == "rating-failure" else "not-evaluated" if mode in ("rating-unknown", "rating-conditional") else "native-pass"
    assert row(report, "SelectedPartRatings")["state"] == expected
    assert report["ratings"]["subject_count"] == len(report["ratings"]["subjects"])
    assert sum(report["ratings"]["counts"].values()) == report["ratings"]["subject_count"]
    for subject in report["ratings"]["subjects"]:
        assert subject["entity_count"] == len(subject["entities"])
        assert subject["observation_count"] == len(subject["observations"])
        assert subject["record_count"] == len(subject["records"])
        assert subject["selection"]["part_digest"].startswith("sha256:")
        assert subject["rule"].startswith("volt.part_erc.")
    for key in ("AssemblyOrientation", "PartPackageReview", "LandPatternReview", "PinMapReview", "AssemblyProcess", "ExportFidelity"):
        assert row(report, key)["state"] == "not-evaluated"
    (tmp_path / "board").write_bytes(artifact)
    (tmp_path / "target").write_text(native["target"])
    del result, artifact, target
    reopened = volt.evaluate_manufacturing_coverage((tmp_path / "board").read_bytes(),
        volt.read_manufacturing_target((tmp_path / "target").read_text()))
    assert reopened.to_json() == native["report"]
    assert (tmp_path / "board").read_bytes() == before
