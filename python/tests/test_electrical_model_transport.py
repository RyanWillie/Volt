import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

import volt


SHARED_EVIDENCE = b"native shared model and V/I evidence"
VI_EVIDENCE = b"native V/I-only evidence"


def _hash(data):
    return "sha256:" + hashlib.sha256(data).hexdigest()


def _produce(path):
    subprocess.run(
        [os.environ["VOLT_PART_MODEL_TRANSPORT_FIXTURE"], str(path)],
        check=True,
        capture_output=True,
    )
    return path


@pytest.fixture
def native_artifacts(tmp_path):
    return _produce(tmp_path / "native")


def test_python_reads_native_model_library_bytes_without_model_authoring(native_artifacts):
    archive = (native_artifacts / "library.voltlib").read_bytes()
    expected = (native_artifacts / "modeled.part.json").read_bytes()
    result = volt._volt.part_library_bundle_part_result(archive, "modeled")

    assert result["bytes"] == expected
    assert result["sha256"] == _hash(expected)
    document = json.loads(result["bytes"])
    assert document["version"] == 6
    model = document["electrical_model"]
    assert model["implements"] == document["implements"] == result["component_sha256"]
    assert model["terminals"] == [
        {"key": "a", "pin_key": "A"},
        {"key": "b", "pin_key": "B"},
    ]
    assert model["internal_nodes"] == [{"key": "after_esl"}, {"key": "after_esr"}]
    elements = {element["key"]: element for element in model["elements"]}
    assert list(elements) == ["esl", "esr", "storage"]
    for key, kind, value in (
        ("esl", "inductance", 1e-9),
        ("esr", "resistance", 0.08),
        ("storage", "capacitance", 1e-5),
    ):
        element = elements[key]
        assert element["kind"] == kind
        assert element["parameter"]["nominal"] == {"dimension": kind, "value": value}
        assert element["parameter"]["evidence"] == [_hash(SHARED_EVIDENCE)]
    assert elements["esr"]["from"] == {"kind": "terminal", "key": "a"}
    assert elements["esr"]["to"] == {"kind": "internal_node", "key": "after_esr"}
    assert elements["esl"]["from"] == elements["esr"]["to"]
    assert elements["esl"]["to"] == {"kind": "internal_node", "key": "after_esl"}
    assert elements["storage"]["from"] == elements["esl"]["to"]
    assert elements["storage"]["to"] == {"kind": "terminal", "key": "b"}
    assert elements["esr"]["parameter"]["tolerance"] is None
    assert elements["esl"]["parameter"]["tolerance"] == {
        "minus": {"dimension": "inductance", "value": 0.0},
        "plus": {"dimension": "inductance", "value": 0.0},
    }
    assert elements["storage"]["parameter"]["tolerance"] == {
        "minus": {"dimension": "capacitance", "value": 1e-5 * 0.1},
        "plus": {"dimension": "capacitance", "value": 1e-5 * 0.2},
    }
    assert document["electrical_records"]["records"][0]["evidence"] == sorted(
        [_hash(SHARED_EVIDENCE), _hash(VI_EVIDENCE)]
    )
    evidence = [
        asset
        for asset in volt._volt.part_library_bundle_part_assets(archive, "modeled")
        if asset["kind"] == "evidence"
    ]
    assert len(evidence) == 2
    assert {asset["bytes"] for asset in evidence} == {SHARED_EVIDENCE, VI_EVIDENCE}
    assert {asset["sha256"] for asset in evidence} == {
        _hash(SHARED_EVIDENCE),
        _hash(VI_EVIDENCE),
    }
    inspection = volt._volt.part_library_bundle_inspect(archive)
    assert sum(entry["role"] == "evidence" for entry in inspection["entries"]) == 3
    absent = volt._volt.part_library_bundle_part_result(archive, "absent")
    assert absent["bytes"] == (native_artifacts / "absent.part.json").read_bytes()
    assert json.loads(absent["bytes"])["electrical_model"] is None


def test_python_opens_complete_native_model_project_without_source_library(native_artifacts):
    expected = (native_artifacts / "modeled.part.json").read_bytes()
    for name in ("library.voltlib", "modeled.part.json", "absent.part.json"):
        (native_artifacts / name).unlink()
    marker = native_artifacts / "source-executed"
    for name in ("project.py", "library.py"):
        (native_artifacts / name).write_text(
            f"from pathlib import Path\nPath({str(marker)!r}).touch()\n"
            "raise AssertionError('source must not execute during native reopen')\n",
            encoding="utf-8",
        )

    path = native_artifacts / "project.volt"
    bundle = volt.ProjectBundle.open(path)
    graph = bundle.graph
    assert not marker.exists()
    assert len(graph.loaded_project.circuits) == 1
    assert graph.loaded_project.circuits[0].component_count == 3
    assert graph.loaded_project.circuits[0].net_count == 2
    assert graph.loaded_project.boards == []
    assert graph.loaded_project.schematics == []
    selected = graph.dependency_lock["selected_parts"]
    assert len(selected) == 2
    artifacts = graph.artifacts
    parts = {
        json.loads(artifact.bytes)["identity"]["name"]: artifact
        for artifact in artifacts
        if artifact.kind == "part_definition"
    }
    evidence = [artifact for artifact in artifacts if artifact.kind == "evidence_asset"]
    assert len(evidence) == 2
    assert {artifact.bytes for artifact in evidence} == {SHARED_EVIDENCE, VI_EVIDENCE}
    assert parts["modeled"].bytes == expected
    assert json.loads(parts["absent"].bytes)["electrical_model"] is None
    dependencies = json.loads(parts["modeled"].manifest_record_json)["depends_on"]
    assert sorted(
        dependency["content_digest"]
        for dependency in dependencies
        if dependency["artifact"]["kind"] == "evidence_asset"
    ) == sorted([_hash(SHARED_EVIDENCE), _hash(VI_EVIDENCE)])

    shutil.rmtree(path)
    del graph
    del bundle
    assert parts["modeled"].bytes == expected
    assert {artifact.bytes for artifact in evidence} == {SHARED_EVIDENCE, VI_EVIDENCE}


def test_native_model_artifact_production_is_byte_deterministic(native_artifacts, tmp_path):
    second = _produce(tmp_path / "second")
    first_files = {
        path.relative_to(native_artifacts): path.read_bytes()
        for path in native_artifacts.rglob("*")
        if path.is_file()
    }
    second_files = {
        path.relative_to(second): path.read_bytes()
        for path in second.rglob("*")
        if path.is_file()
    }
    assert first_files == second_files


def test_native_model_project_rejects_corrupt_evidence_before_exposure(native_artifacts):
    path = native_artifacts / "project.volt"
    manifest = json.loads((path / "manifest.volt.json").read_bytes())
    evidence = next(
        artifact for artifact in manifest["artifacts"] if artifact["kind"] == "evidence_asset"
    )
    (path / Path(evidence["path"])).write_bytes(b"corrupted evidence")
    with pytest.raises(RuntimeError):
        volt.ProjectBundle.open(path)
