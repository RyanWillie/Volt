import importlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory

import volt


def _artifact_texts(artifacts):
    return {
        "logical": artifacts.logical_json.read_text(encoding="utf-8"),
        "schematic": artifacts.schematic_json.read_text(encoding="utf-8"),
        "schematic_svg": artifacts.schematic_svg.read_text(encoding="utf-8"),
        "schematic_body_svg": artifacts.schematic_body_svg.read_text(encoding="utf-8"),
        "pcb": artifacts.pcb_json.read_text(encoding="utf-8"),
        "pcb_svg": artifacts.pcb_svg.read_text(encoding="utf-8"),
        "validation": artifacts.validation_report.read_text(encoding="utf-8"),
        "pages": tuple(path.read_text(encoding="utf-8") for path in artifacts.schematic_svg_pages),
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
        "validation": (artifact_dir / f"{main.EXAMPLE_SLUG}.validation.json").read_text(
            encoding="utf-8"
        ),
        "pages": tuple(
            path.read_text(encoding="utf-8") for path in sorted(pages_dir.glob("*.svg"))
        ),
    }


def test_pcb_led_board_example_uses_object_owned_footprints(monkeypatch, tmp_path):
    main = importlib.import_module("examples.pcb_led_board.main")

    def reject_manual_cache(*_args, **_kwargs):
        raise AssertionError("PCB LED board example should use object-owned footprints")

    monkeypatch.setattr(volt.Board, "cache_footprint", reject_manual_cache)

    artifacts = main.write_artifacts(tmp_path)
    pcb = json.loads(artifacts.pcb_json.read_text(encoding="utf-8"))

    assert len(pcb["board"]["footprint_definitions"]) == 3
    assert len(pcb["viewer"]["pad_resolutions"]) == 6
    assert pcb["viewer"]["diagnostics"] == []


def test_pcb_led_board_example_writes_stable_public_api_artifacts():
    main = importlib.import_module("examples.pcb_led_board.main")

    with TemporaryDirectory() as temp_dir:
        artifacts = main.write_artifacts(Path(temp_dir))
        first_texts = _artifact_texts(artifacts)

        stale_page = artifacts.schematic_svg_pages[0].parent / "stale.svg"
        stale_page.write_text("<svg></svg>\n", encoding="utf-8")
        repeated_artifacts = main.write_artifacts(Path(temp_dir))
        assert not stale_page.exists()
        assert [path.name for path in repeated_artifacts.schematic_svg_pages] == [
            "pcb_led_board_First_Board_LED.svg"
        ]

        second_artifacts = main.write_artifacts(Path(temp_dir) / "second")
        assert _artifact_texts(second_artifacts) == first_texts

    example_dir = Path(main.__file__).resolve().parent
    assert _committed_artifact_texts(main) == first_texts

    logical = json.loads(first_texts["logical"])
    schematic = json.loads(first_texts["schematic"])
    pcb = json.loads(first_texts["pcb"])
    validation = json.loads(first_texts["validation"])

    assert logical["format"] == "volt.logical_circuit"
    assert [component["reference"] for component in logical["components"]] == ["J1", "R1", "D1"]
    assert {net["name"] for net in logical["nets"]} == {"+3V3", "LED_A", "GND"}
    assert {
        component["selected_physical_part"]["footprint"]["name"]
        for component in logical["components"]
    } == {
        "PinHeader_1x02_P2.54mm_Vertical",
        "R_0603_1608Metric",
        "LED_0603_1608Metric",
    }

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
    assert len(pcb["viewer"]["pad_resolutions"]) == 6
    assert pcb["viewer"]["diagnostics"] == []

    assert validation["summary"] == {"errors": 0, "infos": 0, "warnings": 0}
    assert validation["diagnostics"] == []
    assert 'data-board-name="First Board LED"' in first_texts["pcb_svg"]
    assert 'data-placement="component_placement:0"' in first_texts["pcb_svg"]
    assert 'data-net="net:0"' in first_texts["pcb_svg"]

    guide = example_dir / "guide.html"
    guide_text = guide.read_text(encoding="utf-8")
    assert "<!doctype html>" in guide_text
    assert "public Python APIs" in guide_text
    assert "placement-only PCB" in guide_text
