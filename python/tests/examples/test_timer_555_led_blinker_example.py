import importlib
import inspect
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
    assert validation["summary"] == {"errors": 0, "infos": 0, "warnings": 1}
    assert validation["reports"]["logical_design"]["summary"] == {
        "errors": 0,
        "infos": 0,
        "warnings": 0,
    }
    assert validation["reports"]["schematic_readiness"]["summary"] == {
        "errors": 0,
        "infos": 0,
        "warnings": 0,
    }
    assert validation["reports"]["schematic_readability"]["summary"] == {
        "errors": 0,
        "infos": 0,
        "warnings": 1,
    }
    assert {
        (diagnostic["source"], diagnostic["code"]) for diagnostic in validation["diagnostics"]
    } == {("schematic_readability", "SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW")}

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
    symbol_texts_by_definition = {
        definition["name"]: [
            primitive["text"]
            for primitive in definition["primitives"]
            if primitive["type"] == "text"
        ]
        for definition in schematic["symbol_definitions"]
    }
    assert symbol_texts_by_definition["volt.examples.timer_555_led_blinker:NE555"] == [
        "555",
        "timer",
        "DISCH",
        "THRESH",
        "TRIG",
        "OUT",
        "CTRL",
        "RESET",
        "VCC",
        "GND",
    ]
    for definition_name, texts in symbol_texts_by_definition.items():
        if definition_name != "volt.examples.timer_555_led_blinker:NE555":
            assert texts == []
    assert {wire["route_intent"] for wire in schematic["wire_runs"]} == {
        "Direct",
        "Orthogonal",
    }
    assert schematic["sheet_ports"] == []
    assert schematic["no_connect_markers"] == []
    assert len(schematic["symbol_instances"]) == 7
    assert len(schematic["wire_runs"]) >= 12

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
    } >= {"TIMING", "OUT"}
    assert not {
        label.get("label") or net_names_by_id[label["net"]]
        for label in schematic["net_labels"]
    } & {"+5V", "GND"}
    terminal_markers = [
        (port["kind"], net_names_by_id[port["net"]]) for port in schematic["power_ports"]
    ]
    assert terminal_markers.count(("Power", "+5V")) == 2
    assert terminal_markers.count(("Ground", "GND")) == 4

    svg_text = first_texts["svg"]
    assert "<svg xmlns=\"http://www.w3.org/2000/svg\"" in svg_text
    assert 'class="sheet-port off-page"' not in svg_text
    assert 'class="power-port power"' in svg_text
    assert 'class="power-port ground"' in svg_text
    assert "pin-anchor" not in svg_text
    assert "pin-label" not in svg_text
    assert (
        '<text class="title-block-value" x="24" y="40.2" '
        'data-full-text="examples/timer_555_led_blinker/main.py" '
        'textLength="56" lengthAdjust="spacingAndGlyphs">'
        "examples/timer_555_led_blinker/main.py</text>"
    ) in svg_text
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
    visible_texts = re.findall(r">([^<>]+)</text>", svg_text)
    symbol_texts = re.findall(r'<text class="symbol-text"[^>]*>([^<>]+)</text>', svg_text)
    assert not {"R", "C", "D"} & set(symbol_texts)
    for reference in ("U1", "R1", "R2", "C1", "C2", "R3", "D1"):
        assert visible_texts.count(reference) == 1
    assert 'viewBox="0 0 340 240"' in first_texts["pages"][0]


def test_timer_555_led_blinker_schematic_is_readable_without_mutating_logical_design():
    main = importlib.import_module("examples.timer_555_led_blinker.main")
    design, nets, parts = main.build_design()
    logical_before = design.to_json()

    schematic = main.build_schematic(design, nets, parts)
    readiness = schematic.validate()
    readability = schematic.validate_readability()
    readability_codes = {diagnostic.code for diagnostic in readability}

    assert design.to_json() == logical_before
    assert not readiness.has_errors
    assert readability_codes == {"SCHEMATIC_TITLE_BLOCK_TEXT_OVERFLOW"}
    assert not readability.has_errors


def test_timer_555_led_blinker_schematic_uses_generic_anchor_composition():
    main = importlib.import_module("examples.timer_555_led_blinker.main")
    source = inspect.getsource(main.build_schematic)

    assert ".two_terminal(" in source
    assert ".between(" in source
    assert ".tox(" in source
    assert "drawing.ortho_lines(" in source
    assert "drawing.signal_stub(" in source
    assert "drawing.power_stub(" in source
    assert "drawing.ground_stub(" in source
    assert "drawing.connect(nets[" in source
    assert "drawing.junction(nets[" in source
    assert "drawing.node(" not in source
    assert source.count("drawing.connect(nets[") == 1
    assert "drawing.R(" not in source
    assert "drawing.C(" not in source
    assert "drawing.LED(" not in source
    assert "ofst=" not in source
