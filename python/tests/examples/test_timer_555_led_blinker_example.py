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
            "body_svg": artifacts.schematic_body_svg.read_text(encoding="utf-8"),
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
            second_artifacts.schematic_body_svg.read_text(encoding="utf-8")
            == first_texts["body_svg"]
        )
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
    assert validation["summary"] == {"errors": 0, "infos": 0, "warnings": 0}
    assert validation["diagnostics"] == []
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
        "warnings": 0,
    }
    assert {
        (diagnostic["source"], diagnostic["code"]) for diagnostic in validation["diagnostics"]
    } == set()

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
        {"key": "File", "value": "timer_555_led_blinker/main.py"},
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
        "DIS",
        "7",
        "THR",
        "6",
        "TRG",
        "2",
        "OUT",
        "3",
        "CTL",
        "5",
        "RST",
        "4",
        "Vcc",
        "8",
        "GND",
        "1",
    ]
    for definition_name, texts in symbol_texts_by_definition.items():
        if definition_name != "volt.examples.timer_555_led_blinker:NE555":
            assert texts == []
    assert {wire["route_intent"] for wire in schematic["wire_runs"]} == {
        "Direct",
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
        "1 kOhm",
        "D1",
    } <= field_values
    net_names_by_id = {net["id"]: net["name"] for net in logical["nets"]}
    assert {
        label.get("label") or net_names_by_id[label["net"]]
        for label in schematic["net_labels"]
    } == {"TIMING"}
    assert not {
        label.get("label") or net_names_by_id[label["net"]]
        for label in schematic["net_labels"]
    } & {"+5V", "GND"}
    terminal_markers = [
        (port["kind"], net_names_by_id[port["net"]]) for port in schematic["power_ports"]
    ]
    assert terminal_markers.count(("Power", "+5V")) == 2
    assert terminal_markers.count(("Ground", "GND")) == 1
    terminal_positions = [
        (
            port["kind"],
            net_names_by_id[port["net"]],
            port["position"]["x"],
            port["position"]["y"],
        )
        for port in schematic["power_ports"]
    ]
    assert ("Power", "+5V", 178, 54) in terminal_positions
    assert ("Ground", "GND", 170, 152) in terminal_positions
    plus_5v_wire_points = [
        [(point["x"], point["y"]) for point in wire["points"]]
        for wire in schematic["wire_runs"]
        if net_names_by_id[wire["net"]] == "+5V"
    ]
    assert [(162, 74), (178, 74)] in plus_5v_wire_points

    svg_text = first_texts["svg"]
    assert "<svg xmlns=\"http://www.w3.org/2000/svg\"" in svg_text
    assert 'class="sheet-port off-page"' not in svg_text
    assert 'class="power-port power"' in svg_text
    assert 'class="power-port ground"' in svg_text
    assert "pin-anchor" not in svg_text
    assert "pin-label" not in svg_text
    assert "examples/timer_555_led_blinker/main.py" not in svg_text
    assert ">timer_555_led_blinker/main.py</text>" in svg_text
    assert {
        "555",
        "U1",
        "NE555",
        "DIS",
        "THR",
        "TRG",
        "CTL",
        "RST",
        "Vcc",
        "R1",
        "100 kOhm",
        "R2",
        "47 kOhm",
        "C1",
        "1 uF",
        "C2",
        "10 nF",
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
    for reference in ("U1", "R1", "R2", "C1", "C2", "D1"):
        assert visible_texts.count(reference) == 1
    assert visible_texts.count("R3") == 0
    assert 'viewBox="0 0 340 240"' in first_texts["pages"][0]
    assert 'class="schematic-body"' in first_texts["body_svg"]
    assert 'class="title-block"' not in first_texts["body_svg"]
    assert 'class="coordinate-zones"' not in first_texts["body_svg"]
    assert 'viewBox="0 0 340 240"' not in first_texts["body_svg"]
    assert '<text class="symbol-text"' in first_texts["body_svg"]


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
    assert readability_codes == set()
    assert not readability.has_errors


def test_timer_555_led_blinker_schematic_uses_generic_anchor_composition():
    main = importlib.import_module("examples.timer_555_led_blinker.main")
    source = inspect.getsource(main.build_schematic)

    assert ".two_terminal(" in source
    assert ".endpoints(" in source
    assert ".to(" in source
    assert ".toy(" in source
    assert ".tox(" in source
    assert ".dot()" in source
    assert ".idot()" in source
    assert "drawing.local_label(" in source
    assert "drawing.power_stub(" in source
    assert "drawing.ground_stub(" in source
    assert "drawing.wire(nets[" in source
    assert "drawing.node(" not in source
    assert ".between(" not in source
    assert "drawing.ortho_lines(" not in source
    assert "drawing.connect(nets[" not in source
    assert "drawing.junction(nets[" not in source
    assert "drawing.R(" not in source
    assert "drawing.C(" not in source
    assert "drawing.LED(" not in source
    assert "ofst=" not in source
