import importlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory


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


def test_routed_regulator_led_board_example_writes_stable_board_artifacts():
    main = importlib.import_module("examples.routed_regulator_led_board.main")

    with TemporaryDirectory() as temp_dir:
        artifacts = main.write_artifacts(Path(temp_dir))
        first_texts = _artifact_texts(artifacts)

        stale_page = artifacts.schematic_svg_pages[0].parent / "stale.svg"
        stale_page.write_text("<svg></svg>\n", encoding="utf-8")
        repeated_artifacts = main.write_artifacts(Path(temp_dir))
        assert not stale_page.exists()
        assert [path.name for path in repeated_artifacts.schematic_svg_pages] == [
            "routed_regulator_led_board_Regulator_LED_Board.svg"
        ]

        second_artifacts = main.write_artifacts(Path(temp_dir) / "second")
        assert _artifact_texts(second_artifacts) == first_texts

    assert _committed_artifact_texts(main) == first_texts

    logical = json.loads(first_texts["logical"])
    schematic = json.loads(first_texts["schematic"])
    pcb = json.loads(first_texts["pcb"])
    validation = json.loads(first_texts["validation"])

    assert logical["format"] == "volt.logical_circuit"
    assert [component["reference"] for component in logical["components"]] == [
        "J1",
        "U1",
        "C1",
        "C2",
        "R1",
        "D1",
    ]
    assert {net["name"] for net in logical["nets"]} == {"VIN", "+3V3", "LED_A", "GND"}
    assert {
        component["selected_physical_part"]["footprint"]["name"]
        for component in logical["components"]
    } == {
        "PinHeader_1x02_P2.54mm_Vertical",
        "SOT-223-3_TabPin2",
        "C_0603_1608Metric",
        "R_0603_1608Metric",
        "LED_0603_1608Metric",
    }

    assert schematic["format"] == "volt.schematic"
    assert [sheet["name"] for sheet in schematic["sheets"]] == ["Regulator LED Board"]
    assert len(schematic["symbol_instances"]) == 6

    assert pcb["format"] == "volt.pcb"
    assert pcb["board"]["name"] == "Routed Regulator LED Board"
    assert [layer["name"] for layer in pcb["board"]["layers"]] == [
        "F.Cu",
        "B.Cu",
        "F.SilkS",
    ]
    assert pcb["board"]["layer_stack"]["layers"] == ["board_layer:0", "board_layer:1"]
    assert pcb["board"]["outline"]["vertices"] == [[0, 0], [70, 0], [70, 42], [0, 42]]
    assert [feature["label"] for feature in pcb["board"]["features"]] == [
        "MH1",
        "MH2",
        "MH3",
        "MH4",
    ]
    assert pcb["board"]["rules"] == {
        "board_outline_clearance_mm": 0.25,
        "copper_clearance_mm": 0.20,
        "minimum_track_width_mm": 0.25,
        "minimum_via_annular_diameter_mm": 0.70,
        "minimum_via_drill_diameter_mm": 0.30,
    }
    assert len(pcb["board"]["placements"]) == 6
    assert len(pcb["board"]["footprint_definitions"]) == 5
    assert len(pcb["board"]["tracks"]) >= 8
    assert len(pcb["board"]["vias"]) >= 1
    assert len(pcb["viewer"]["pad_resolutions"]) == 14
    assert pcb["viewer"]["diagnostics"] == []

    assert validation["summary"] == {"errors": 0, "infos": 0, "warnings": 0}
    assert validation["reports"]["pcb_board"]["summary"] == {
        "errors": 0,
        "infos": 0,
        "warnings": 0,
    }
    assert validation["diagnostics"] == []
    assert 'data-board-name="Routed Regulator LED Board"' in first_texts["pcb_svg"]
    assert 'data-track="board_track:0"' in first_texts["pcb_svg"]
    assert 'data-via="board_via:0"' in first_texts["pcb_svg"]

    note_text = (Path(main.__file__).resolve().parent / "guide.html").read_text(
        encoding="utf-8"
    )
    assert "<!doctype html>" in note_text
    assert "routed real-board PCB benchmark" in note_text
    assert "public Python APIs" in note_text
    assert "kernel-owned PCB model" in note_text
    assert "Out of scope" in note_text
