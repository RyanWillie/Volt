"""Synthetic assertions prove exact native correspondence, never actual human inspection."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

import pytest
import volt


def fixture(mode="evidence-fab"):
    return json.loads(subprocess.check_output(
        [os.environ["VOLT_MANUFACTURING_PARITY_FIXTURE"], mode], text=True,
    ))


def inputs(data):
    artifact = bytes.fromhex(data["artifact_hex"])
    target = volt.read_manufacturing_target(data["target"])
    evidence = volt.ManufacturingEvidenceInput()
    evidence.assertions = volt.read_manufacturing_evidence(data["evidence"])
    evidence.attachments = [
        volt.ManufacturingEvidenceAttachment("source", data["source_bytes"].encode()),
        volt.ManufacturingEvidenceAttachment("review", data["review_bytes"].encode()),
    ]
    return artifact, target, evidence


@pytest.mark.parametrize("mode", ["evidence-fab", "evidence-assembly", "evidence-blocked", "evidence-bottom", "evidence-range"])
def test_full_native_report_and_file_bytes_source_free(mode, tmp_path):
    data = fixture(mode)
    artifact, target, evidence = inputs(data)
    result = volt.evaluate_manufacturing_export(artifact, target, evidence)
    assert isinstance(result, volt.ManufacturingEvaluation)
    assert result.to_json() == data["report"]
    report = json.loads(result.to_json())
    assert len(report["coverage"]) == 65
    assert report["schema_version"] == 3 and report["policy_version"] == 1
    assert not result.ready
    assert len(result.files) == len(report["native_export"]["files"])
    assert len(report["completion"]["files"]) == len(result.files)
    if mode == "evidence-range":
        assert report["native_export"]["losses"]
        assert report["completion"]["diagnostics"]
    for native, emitted in zip(result.files, report["native_export"]["files"]):
        assert native.bytes == emitted["text"].encode()
        assert native.filename == emitted["filename"] and native.function == emitted["function"]
        assert "sha256:" + hashlib.sha256(native.bytes).hexdigest() == emitted["digest"]
    assert volt.write_manufacturing_evidence(evidence.assertions) == data["evidence"]
    if mode in ("evidence-blocked", "evidence-range"):
        assert all(r["state"] != "reviewed-pass" for r in report["coverage"])
    else:
        assert report["counts"]["reviewed_pass"] == (12 if mode == "evidence-fab" else 16)
        assert all(r["disposition"] == "accepted" for r in report["reviews"])
    # Actual evaluation in another directory without source owners or source imports.
    (tmp_path / "input.json").write_text(json.dumps(data))
    runner = """
import json
from pathlib import Path
import volt
x=json.loads(Path('input.json').read_text())
i=volt.ManufacturingEvidenceInput()
i.assertions=volt.read_manufacturing_evidence(x['evidence'])
i.attachments=[volt.ManufacturingEvidenceAttachment('source', x['source_bytes'].encode()),
               volt.ManufacturingEvidenceAttachment('review', x['review_bytes'].encode())]
a=bytes.fromhex(x['artifact_hex']); before=bytes(a)
r=volt.evaluate_manufacturing_export(a,volt.read_manufacturing_target(x['target']),i)
assert r.to_json()==x['report'] and a==before
"""
    subprocess.run([sys.executable, "-c", runner], cwd=tmp_path, check=True)
    assert artifact == bytes.fromhex(data["artifact_hex"])


def test_native_typed_factory_derives_complete_scope_and_method():
    data = fixture()
    artifact, target, evidence = inputs(data)
    initial = volt.evaluate_manufacturing_export(artifact, target)
    assertion = volt.make_manufacturing_review(
        artifact, initial, volt.ManufacturingCheck.LegendHeight,
        volt.ManufacturingReviewOutcome.Pass, "Synthetic fixture, not a reviewer",
        "synthetic://test/profile-a", "source", "Synthetic minimum; not vendor policy", 0.15,
        "Synthetic observation", "Binding only", "review",
        "sha256:" + hashlib.sha256(data["review_bytes"].encode()).hexdigest(),
    )
    evidence.assertions = [assertion]
    result = volt.evaluate_manufacturing_export(artifact, target, evidence)
    report = json.loads(result.to_json())
    assert report["reviews"][0]["disposition"] == "accepted"
    assert report["counts"]["reviewed_pass"] == 1
    assert report["reviews"][0]["assertion"]["scope_count"] > 1


@pytest.mark.parametrize("variant", ["file", "missing", "extra", "empty", "source", "review", "basename"])
def test_every_supplied_byte_and_file_is_checked(variant):
    data = fixture()
    artifact, target, evidence = inputs(data)
    emitted = volt.evaluate_manufacturing_export(artifact, target).files
    if variant == "file":
        emitted[0].bytes += b"x"
    elif variant == "missing":
        emitted.pop()
    elif variant == "extra":
        emitted.append(emitted[0])
    elif variant == "empty":
        emitted = []
    elif variant in ("source", "review"):
        attachments = evidence.attachments
        attachments[0 if variant == "source" else 1].bytes += b"x"
        evidence.attachments = attachments
    elif variant == "basename":
        evidence.basename = "changed"
    evidence.reviewed_files = emitted
    report = json.loads(volt.evaluate_manufacturing_export(artifact, target, evidence).to_json())
    assert all(r["state"] != "reviewed-pass" for r in report["coverage"])
    assert all(r["disposition"] != "accepted" for r in report["reviews"])


@pytest.mark.parametrize("variant", ["unknown", "duplicate", "scope", "hash", "version", "criteria"])
def test_strict_typed_evidence_errors(variant):
    doc = json.loads(fixture()["evidence"])
    if variant == "unknown":
        doc["unexpected"] = 1
    elif variant == "duplicate":
        doc["assertions"].append(doc["assertions"][0])
    elif variant == "scope":
        doc["assertions"][0]["scope_count"] += 1
    elif variant == "hash":
        doc["assertions"][0]["evidence_digest"] = "bad"
    elif variant == "version":
        doc["schema_version"] = 2
    elif variant == "criteria":
        doc["assertions"][1]["minimum_mm"] = None
    with pytest.raises(volt.ManufacturingEvidenceReadError) as error:
        volt.read_manufacturing_evidence(json.dumps(doc))
    assert error.value.code == ("UnsupportedVersion" if variant == "version" else "Malformed")
