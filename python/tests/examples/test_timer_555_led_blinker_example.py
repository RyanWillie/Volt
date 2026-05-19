import importlib
import json
import re
from pathlib import Path
from tempfile import TemporaryDirectory


def test_timer_555_led_blinker_example_writes_stable_artifacts():
    main = importlib.import_module("examples.timer_555_led_blinker.main")

    with TemporaryDirectory() as temp_dir:
        artifacts = main.write_artifacts(Path(temp_dir))
        logical = json.loads(artifacts.logical_json.read_text(encoding="utf-8"))
        schematic = json.loads(artifacts.schematic_json.read_text(encoding="utf-8"))
        validation = json.loads(artifacts.validation_report.read_text(encoding="utf-8"))
        first_texts = {
            "logical": artifacts.logical_json.read_text(encoding="utf-8"),
            "schematic": artifacts.schematic_json.read_text(encoding="utf-8"),
            "svg": artifacts.schematic_svg.read_text(encoding="utf-8"),
            "validation": artifacts.validation_report.read_text(encoding="utf-8"),
            "pages": tuple(path.read_text(encoding="utf-8") for path in artifacts.schematic_svg_pages),
        }

        stale_page = artifacts.schematic_svg_pages[0].parent / "stale.svg"
        stale_page.write_text("<svg></svg>\n", encoding="utf-8")
        repeated_artifacts = main.write_artifacts(Path(temp_dir))
        assert not stale_page.exists()
        assert [path.name for path in repeated_artifacts.schematic_svg_pages] == [
            "timer_555_led_blinker_555_LED_Blinker.svg"
        ]

        second_artifacts = main.write_artifacts(Path(temp_dir) / "second")
        assert second_artifacts.logical_json.read_text(encoding="utf-8") == first_texts["logical"]
        assert (
            second_artifacts.schematic_json.read_text(encoding="utf-8")
            == first_texts["schematic"]
        )
        assert second_artifacts.schematic_svg.read_text(encoding="utf-8") == first_texts["svg"]
        assert (
            second_artifacts.validation_report.read_text(encoding="utf-8")
            == first_texts["validation"]
        )
        assert (
            tuple(path.read_text(encoding="utf-8") for path in second_artifacts.schematic_svg_pages)
            == first_texts["pages"]
        )

    assert logical["format"] == "volt.logical_circuit"
    assert {component["reference"] for component in logical["components"]} == {
        "U1",
        "R1",
        "R2",
        "C1",
        "C2",
        "R3",
        "D1",
    }
    assert {net["name"] for net in logical["nets"]} == {
        "+5V",
        "GND",
        "DISCH",
        "TIMING",
        "CTRL",
        "OUT",
        "LED_A",
    }
    assert sum(validation["summary"].values()) == len(validation["diagnostics"])

    assert schematic["format"] == "volt.schematic"
    assert [sheet["name"] for sheet in schematic["sheets"]] == ["555 LED Blinker"]
    sheet = schematic["sheets"][0]
    assert sheet["metadata"]["title"] == "555 LED Blinker Reference Schematic"
    assert sheet["metadata"]["title_block"] == [
        {"key": "Number", "value": "1"},
        {"key": "Page Count", "value": "1"},
        {"key": "Revision", "value": "A"},
        {"key": "Date", "value": "2026-05-19"},
        {"key": "Project", "value": "Volt 555 LED Blinker"},
        {"key": "File", "value": "examples/timer_555_led_blinker/main.py"},
    ]
    assert {definition["name"] for definition in schematic["symbol_definitions"]} >= {
        "volt.examples.timer_555_led_blinker:NE555"
    }
    timer_symbol = next(
        definition
        for definition in schematic["symbol_definitions"]
        if definition["name"] == "volt.examples.timer_555_led_blinker:NE555"
    )
    assert [pin["name"] for pin in timer_symbol["pins"]] == [
        "DISCH",
        "THRESH",
        "TRIG",
        "OUT",
        "CTRL",
        "RESET",
        "VCC",
        "GND",
    ]
    assert {wire["route_intent"] for wire in schematic["wire_runs"]} == {"Orthogonal"}
    assert schematic["sheet_ports"] == []
    assert schematic["power_ports"] == []
    assert schematic["no_connect_markers"] == []
    assert len(schematic["symbol_instances"]) == 7
    assert len(schematic["wire_runs"]) >= 14

    field_values = {field["value"] for field in schematic["symbol_fields"]}
    assert {
        "U1",
        "NE555",
        "R1",
        "100 kOhm",
        "R2",
        "47 kOhm",
        "C1",
        "1 uF",
        "C2",
        "10 nF",
        "R3",
        "1 kOhm",
        "D1",
    } <= field_values
    net_names_by_id = {net["id"]: net["name"] for net in logical["nets"]}
    assert {
        label.get("label") or net_names_by_id[label["net"]]
        for label in schematic["net_labels"]
    } >= {"TIMING", "OUT", "+5V", "GND"}

    svg_text = first_texts["svg"]
    assert "<svg xmlns=\"http://www.w3.org/2000/svg\"" in svg_text
    assert 'class="sheet-port off-page"' not in svg_text
    assert "pin-anchor" not in svg_text
    assert "pin-label" not in svg_text
    assert {
        "555",
        "timer",
        "U1",
        "NE555",
        "R1",
        "100 kOhm",
        "R2",
        "47 kOhm",
        "C1",
        "1 uF",
        "C2",
        "10 nF",
        "R3",
        "1 kOhm",
        "D1",
        "TIMING",
        "OUT",
        "+5V",
        "GND",
    } <= set(re.findall(r">([^<>]+)</text>", svg_text))
    assert 'viewBox="0 0 340 240"' in first_texts["pages"][0]


def test_timer_555_led_blinker_schematic_is_readable_without_mutating_logical_design():
    main = importlib.import_module("examples.timer_555_led_blinker.main")
    design, nets, parts = main.build_design()
    logical_before = design.to_json()

    schematic = main.build_schematic(design, nets, parts)
    readiness = schematic.validate()
    readability = schematic.validate_readability()

    assert design.to_json() == logical_before
    assert not readiness.has_errors
    assert len(readability) == 0
    assert not readability.has_errors
