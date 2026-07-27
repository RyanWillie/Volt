import importlib
import inspect
import json
from pathlib import Path
from tempfile import TemporaryDirectory

import pytest
import volt


def test_pcb_led_board_example_exposes_project_result():
    main = importlib.import_module("examples.pcb_led_board.main")

    result = main.run_project()

    assert isinstance(result, volt.ProjectResult)
    assert result.ok
    assert [stage.name for stage in result.stages] == ["design", "schematic", "board"]
    assert result.design().name == "pcb-led-board"
    assert result.schematic().name == "First Board LED"
    assert result.board().name == "First Board LED"


def test_pcb_led_board_project_stages_are_primary_authoring_functions():
    main = importlib.import_module("examples.pcb_led_board.main")

    source = inspect.getsource(main.build_project)

    assert "return author_schematic(" in source
    assert "return build_board(" in source
    assert 'context.resource("nets", dict)' in source
    assert 'context.resource("parts", dict)' in source


def _artifact_texts(artifacts):
    project_bundle = artifacts.logical_json.parent / "pcb_led_board.volt"
    return {
        "logical": artifacts.logical_json.read_text(encoding="utf-8"),
        "schematic": artifacts.schematic_json.read_text(encoding="utf-8"),
        "schematic_svg": artifacts.schematic_svg.read_text(encoding="utf-8"),
        "schematic_body_svg": artifacts.schematic_body_svg.read_text(encoding="utf-8"),
        "pcb": artifacts.pcb_json.read_text(encoding="utf-8"),
        "pcb_svg": artifacts.pcb_svg.read_text(encoding="utf-8"),
        "kicad_pcb": artifacts.kicad_pcb.read_text(encoding="utf-8"),
        "cpl_json": artifacts.cpl_json.read_text(encoding="utf-8"),
        "cpl_csv": artifacts.cpl_csv.read_text(encoding="utf-8"),
        "validation": artifacts.diagnostics_json.read_text(encoding="utf-8"),
        "project": _project_bundle_texts(project_bundle),
        "pages": tuple(path.read_text(encoding="utf-8") for path in artifacts.schematic_svg_pages),
    }


def _project_bundle_texts(bundle):
    return {
        path.relative_to(bundle).as_posix(): path.read_bytes()
        for path in sorted(bundle.rglob("*"))
        if path.is_file()
    }


def _committed_artifact_texts(main):
    example_dir = Path(main.__file__).resolve().parent
    artifact_dir = example_dir / "artifacts"
    pages_dir = artifact_dir / f"{main.EXAMPLE_SLUG}.pages"
    return {
        "logical": (artifact_dir / f"{main.EXAMPLE_SLUG}.volt.json").read_text(
            encoding="utf-8"
        ),
        "schematic": (artifact_dir / f"{main.EXAMPLE_SLUG}.volt.schematic.json").read_text(
            encoding="utf-8"
        ),
        "schematic_svg": (artifact_dir / f"{main.EXAMPLE_SLUG}.svg").read_text(
            encoding="utf-8"
        ),
        "schematic_body_svg": (artifact_dir / f"{main.EXAMPLE_SLUG}.body.svg").read_text(
            encoding="utf-8"
        ),
        "pcb": (artifact_dir / f"{main.EXAMPLE_SLUG}.volt.pcb.json").read_text(
            encoding="utf-8"
        ),
        "pcb_svg": (artifact_dir / f"{main.EXAMPLE_SLUG}.pcb.svg").read_text(
            encoding="utf-8"
        ),
        "kicad_pcb": (artifact_dir / f"{main.EXAMPLE_SLUG}.kicad_pcb").read_text(
            encoding="utf-8"
        ),
        "cpl_json": (artifact_dir / f"{main.EXAMPLE_SLUG}.cpl.json").read_text(
            encoding="utf-8"
        ),
        "cpl_csv": (artifact_dir / f"{main.EXAMPLE_SLUG}.cpl.csv").read_text(
            encoding="utf-8"
        ),
        "validation": (artifact_dir / f"{main.EXAMPLE_SLUG}.validation.json").read_text(
            encoding="utf-8"
        ),
        "project": _project_bundle_texts(artifact_dir / f"{main.EXAMPLE_SLUG}.volt"),
        "pages": tuple(
            path.read_text(encoding="utf-8") for path in sorted(pages_dir.glob("*.svg"))
        ),
    }


def test_pcb_led_board_example_uses_native_explicit_footprints(tmp_path):
    main = importlib.import_module("examples.pcb_led_board.main")

    artifacts = main.write_artifacts(tmp_path)
    pcb = json.loads(artifacts.pcb_json.read_text(encoding="utf-8"))

    assert len(pcb["board"]["footprint_definitions"]) == 3
    assert "viewer" not in pcb
    assert artifacts.pcb_svg.read_text(encoding="utf-8").count(
        '<circle class="pad-overlay '
    ) == 6


def test_pcb_led_board_example_writes_stable_public_api_artifacts():
    main = importlib.import_module("examples.pcb_led_board.main")

    with TemporaryDirectory() as temp_dir:
        artifacts = main.write_artifacts(Path(temp_dir))
        first_texts = _artifact_texts(artifacts)

        stale_page = artifacts.schematic_svg_pages[0].parent / "stale.svg"
        stale_page.write_text("<svg></svg>\n", encoding="utf-8")
        with pytest.raises(RuntimeError, match="destination already exists"):
            main.write_artifacts(Path(temp_dir))
        assert stale_page.exists()

        second_artifacts = main.write_artifacts(Path(temp_dir) / "second")
        assert _artifact_texts(second_artifacts) == first_texts

    committed = _committed_artifact_texts(main)
    assert {key: value for key, value in committed.items() if key != "project"} == {
        key: value for key, value in first_texts.items() if key != "project"
    }

    logical = json.loads(first_texts["logical"])
    schematic = json.loads(first_texts["schematic"])
    pcb = json.loads(first_texts["pcb"])
    validation = json.loads(first_texts["validation"])

    assert logical["format"] == "volt.logical_circuit"
    assert [component["reference"] for component in logical["components"]] == ["J1", "R1", "D1"]
    assert {net["name"] for net in logical["nets"]} == {"+3V3", "LED_A", "GND"}
    assert all(
        component["selected_library_part"]["library_namespace"]
        == "volt.python.design"
        for component in logical["components"]
    )
    resistor = next(
        component for component in logical["components"] if component["reference"] == "R1"
    )
    assert resistor["selected_library_part"]["part_digest"].startswith("sha256:")

    assert schematic["format"] == "volt.schematic"
    assert [sheet["name"] for sheet in schematic["sheets"]] == ["First Board LED"]
    assert len(schematic["symbol_instances"]) == 3
    assert len(schematic["wire_runs"]) >= 3

    assert pcb["format"] == "volt.pcb"
    assert pcb["board"]["name"] == "First Board LED"
    assert [layer["name"] for layer in pcb["board"]["layers"]] == ["F.Cu", "B.Cu"]
    assert pcb["board"]["outline"]["vertices"] == [[0, 0], [32, 0], [32, 18], [0, 18]]
    assert [feature["label"] for feature in pcb["board"]["features"]] == ["MH1", "MH2"]
    assert [placement["component"] for placement in pcb["board"]["placements"]] == [
        "component:0",
        "component:1",
        "component:2",
    ]
    assert len(pcb["board"]["footprint_definitions"]) == 3
    assert "viewer" not in pcb
    assert first_texts["pcb_svg"].count('<circle class="pad-overlay ') == 6

    assert validation["summary"] == {"errors": 0, "infos": 0, "warnings": 0}
    assert validation["diagnostics"] == []
    project_manifest = json.loads(first_texts["project"]["manifest.volt.json"])
    assert project_manifest["format"] == "volt.project_result"
    assert project_manifest["schema_version"] == 2
    assert project_manifest["run"]["ok"] is True
    assert project_manifest["export_selection"] == []
    kinds = [artifact["kind"] for artifact in project_manifest["artifacts"]]
    assert kinds.count("logical_model") == 1
    assert kinds.count("board_model") == 1
    assert kinds.count("compiled_board") == 1
    assert kinds.count("board_scene") == 1
    tests_artifact = next(
        artifact
        for artifact in project_manifest["artifacts"]
        if artifact["kind"] == "project_tests"
    )
    project_tests = json.loads(first_texts["project"][tests_artifact["path"]])
    assert project_tests["summary"] == {"failed": 0, "passed": 3}
    assert 'data-board-name="First Board LED"' in first_texts["pcb_svg"]
    assert 'data-placement="component_placement:0"' in first_texts["pcb_svg"]
    assert 'data-net="net:0"' in first_texts["pcb_svg"]
    assert first_texts["kicad_pcb"].startswith("(kicad_pcb\n")
    assert '(generator "Volt")' in first_texts["kicad_pcb"]
    assert '(footprint "PinHeader_1x02_P2.54mm_Vertical"' in first_texts["kicad_pcb"]
    assert '(49 "F.Fab" user)' in first_texts["kicad_pcb"]
    assert '(segment\n    (start 5 7.73)' in first_texts["kicad_pcb"]

    example_dir = Path(main.__file__).resolve().parent
    guide = example_dir / "guide.html"
    guide_text = guide.read_text(encoding="utf-8")
    assert "<!doctype html>" in guide_text
    assert "public Python APIs" in guide_text
    assert "placement-only PCB" in guide_text
