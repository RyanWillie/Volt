import json

import pytest

import volt


def _bundle(tmp_path):
    project = volt.Project("reopen")

    @project.design
    def design():
        return volt.Design("main")

    path = tmp_path / "reopen.volt"
    project.run().write(path)
    return path


def test_project_bundle_v2_python_view_matches_verified_native_graph(tmp_path):
    path = _bundle(tmp_path)
    manifest = json.loads((path / "manifest.volt.json").read_text(encoding="utf-8"))

    bundle = volt.ProjectBundle.open(path)
    assert bundle.schema_version == volt.ProjectBundleSchemaVersion.V2
    assert bundle.storage_kind == volt.ProjectBundleStorageKind.DIRECTORY
    assert bundle.integrity_status == volt.BundleIntegrityStatus.VERIFIED_V2

    graph = bundle.v2
    assert graph.project_name == "reopen"
    assert graph.build_id == manifest["build_id"]
    assert graph.bundle_digest == manifest["bundle_digest"]
    assert graph.authoring_inputs_digest == manifest["authoring_inputs"]["digest"]
    assert graph.dependency_lock == manifest["dependency_lock"]
    assert json.loads(graph.manifest_bytes) == manifest
    assert [artifact.kind for artifact in graph.artifacts] == [
        artifact["kind"] for artifact in manifest["artifacts"]
    ]
    assert [json.loads(artifact.id_json) for artifact in graph.artifacts] == [
        artifact["id"] for artifact in manifest["artifacts"]
    ]
    first = graph.artifacts[0]
    assert isinstance(first.id, volt.ArtifactId)
    assert graph.artifact(first.id).id == first.id
    assert len(graph.loaded_project.circuits) == 1
    assert graph.loaded_project.circuits[0].design == "main"
    assert graph.loaded_project.circuits[0].artifact.bytes
    assert graph.loaded_project.selected_exports == []
    assert graph.loaded_project.diagnostics.bytes
    assert graph.loaded_project.tests.bytes

    retained = graph.loaded_project.circuits[0]
    del graph
    del bundle
    assert retained.design == "main"
    assert retained.artifact.bytes


@pytest.mark.parametrize("source_name", ["project.py", "library.py"])
def test_project_bundle_v2_never_executes_project_or_library_source(tmp_path, source_name):
    path = _bundle(tmp_path)
    sentinel = tmp_path / f"{source_name}.executed"
    (path / source_name).write_text(
        "from pathlib import Path\n"
        f"Path({str(sentinel)!r}).write_text('executed')\n",
        encoding="utf-8",
    )

    with pytest.raises(RuntimeError):
        volt.ProjectBundle.open(path)
    assert not sentinel.exists()
