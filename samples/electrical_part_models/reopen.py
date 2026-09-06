"""Inspect a verified saved project without importing its authoring code or library."""

import json
from pathlib import Path
import sys

import volt


def inspect_example(destination: Path) -> None:
    expected = json.loads((destination / "expected.json").read_text(encoding="utf-8"))
    bundle = volt.ProjectBundle.open(destination / "project.volt")
    graph = bundle.graph
    loaded = graph.loaded_project
    assert len(loaded.circuits) == 1
    assert loaded.circuits[0].component_count == 5
    assert loaded.circuits[0].net_count == 2
    assert loaded.boards == loaded.schematics == []
    logical = json.loads(loaded.circuits[0].artifact.bytes)
    for instance in logical["components"]:
        reference = instance["selected_library_part"]
        assert reference == expected["parts"][reference["part_key"]]["exact_reference"]
    parts = {}
    evidence = set()
    for artifact in graph.artifacts:
        if artifact.kind == "evidence_asset":
            evidence.add(artifact.bytes.decode("utf-8"))
        elif artifact.kind == "part_definition":
            document = json.loads(artifact.bytes)
            name = document["identity"]["name"]
            assert artifact.bytes.decode("utf-8") == expected["parts"][name]["bytes"]
            parts[name] = document
    assert set(parts) == set(expected["parts"])
    assert evidence == set(expected["evidence"])

    # These are canonical native-validated artifacts, decoded only for display/assertions.
    models = {name: part["electrical_model"] for name, part in parts.items()}
    assert models["unmodeled"] is None
    for name, model in models.items():
        if model is not None:
            assert model["implements"] == parts[name]["implements"]
            assert model["terminals"] == [
                {"key": "a", "pin_key": "A"}, {"key": "b", "pin_key": "B"}
            ]
    r = models["R330"]["elements"][0]["parameter"]
    assert r["nominal"] == {"dimension": "resistance", "value": 330.0}
    assert r["tolerance"]["minus"]["value"] == 330.0 * 0.01
    assert r["tolerance"]["plus"]["value"] == 330.0 * 0.01
    assert r["evidence"]
    c = models["C-ideal"]["elements"][0]["parameter"]
    assert c["nominal"] == {"dimension": "capacitance", "value": 100e-9}
    assert c["tolerance"] is None  # Unspecified, not exact.
    l = models["L-ideal"]["elements"][0]["parameter"]
    assert l["nominal"] == {"dimension": "inductance", "value": 10e-6}
    assert l["tolerance"]["minus"]["value"] == l["tolerance"]["plus"]["value"] == 0

    composite = models["C-ESR-ESL"]
    assert composite["internal_nodes"] == [{"key": "after_esl"}, {"key": "after_esr"}]
    elements = {element["key"]: element for element in composite["elements"]}
    assert list(elements) == ["esl", "esr", "storage"]
    assert elements["esr"]["from"] == {"kind": "terminal", "key": "a"}
    assert elements["esr"]["to"] == elements["esl"]["from"] == {
        "kind": "internal_node", "key": "after_esr"
    }
    assert elements["esl"]["to"] == elements["storage"]["from"] == {
        "kind": "internal_node", "key": "after_esl"
    }
    assert elements["storage"]["to"] == {"kind": "terminal", "key": "b"}
    for key, value in (("esr", 0.08), ("esl", 1e-9), ("storage", 10e-6)):
        assert elements[key]["parameter"]["nominal"]["value"] == value
    storage = elements["storage"]["parameter"]
    assert storage["tolerance"]["minus"]["value"] == 10e-6 * 0.2
    assert storage["tolerance"]["plus"]["value"] == 10e-6 * 0.2
    assert storage["evidence"] == r["evidence"]
    assert len(parts["C-ESR-ESL"]["electrical_records"]["records"][0]["evidence"]) == 2
    print("Reopened five exact Parts: R, ideal C/L, one composite, and one absent model.")


if __name__ == "__main__":
    inspect_example(Path(sys.argv[1]))
