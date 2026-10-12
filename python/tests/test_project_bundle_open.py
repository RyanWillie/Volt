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
    assert bundle.schema_version == volt.ProjectBundleSchemaVersion.V3
    assert bundle.storage_kind == volt.ProjectBundleStorageKind.DIRECTORY
    graph = bundle.graph
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


def test_hierarchical_compiled_board_v2_reopens_through_native_project_bundle(tmp_path):
    project = volt.Project("hierarchical-continuity")

    @project.design
    def design():
        logical = volt.Design("main")
        module = logical.define_module("Child")
        module.port("N")
        child = logical.instantiate(module, ref="M")
        parent = logical.net("PARENT")
        parent += child["N"]
        return logical

    @project.board
    def board(context):
        logical = context.design()
        nets = {net.name: net for net in logical.nets()}
        pcb = logical.add_board("Main")
        front = pcb.add_layer("F.Cu", role="copper", side="top")
        pcb.set_layer_stack((front,), thickness=1.6)
        pcb.set_rectangular_outline(origin=(0.0, 0.0), size=(12.0, 6.0))
        pcb.set_capability_profile(
            volt.CapabilityProfile(
                name="Native fixture",
                source="Native test",
                as_of="2026-10-11",
                minimum_track_width=0.1,
                minimum_via_drill=0.2,
                minimum_via_annular=0.4,
            )
        )
        pcb.add_track(nets["PARENT"], layer=front, points=((2.0, 3.0), (10.0, 3.0)), width=0.4)
        pcb.add_track(nets["M/N"], layer=front, points=((4.0, 3.0), (8.0, 3.0)), width=0.4)
        return pcb

    result = project.run()
    assert "PCB_COPPER_CLEARANCE_VIOLATION" not in {
        diagnostic.code for diagnostic in result.diagnostics
    }
    path = tmp_path / "hierarchical.volt"
    result.write(path)
    manifest = json.loads((path / "manifest.volt.json").read_text(encoding="utf-8"))
    compiled_record = next(
        artifact for artifact in manifest["artifacts"] if artifact["kind"] == "compiled_board"
    )
    assert compiled_record["schema"]["version"] == 2
    archived = (path / compiled_record["path"]).read_bytes()
    assert b'"realized_net_partition":[["net:0","net:1"]]' in archived
    del result
    del project

    reopened = volt.ProjectBundle.open(path)
    loaded = reopened.graph.loaded_project.compiled_boards
    assert len(loaded) == 1
    assert loaded[0].artifact.bytes == archived
    assert json.loads(loaded[0].artifact.manifest_record_json)["schema"]["version"] == 2
