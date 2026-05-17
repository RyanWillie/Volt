import importlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory


def _compact_led():
    return importlib.import_module("examples.schematic_sugar.compact_led")


def test_compact_led_example_builds_readable_logical_design():
    compact_led = _compact_led()
    design, nets, parts = compact_led.build_design()

    report = design.validate()
    logical = json.loads(design.to_json())

    assert not report.has_errors
    assert [diagnostic.code for diagnostic in report] == [
        "SINGLE_PIN_NET",
        "SINGLE_PIN_NET",
    ]
    assert {net["name"] for net in logical["nets"]} == {"+3V3", "LED_A", "GND"}
    assert [component["reference"] for component in logical["components"]] == ["R1", "D1"]
    assert tuple(pin.index for pin in nets["LED_A"].pins()) == (
        parts["R1"][2].index,
        parts["D1"]["A"].index,
    )


def test_compact_led_example_renders_schematic_without_mutating_logical_design():
    compact_led = _compact_led()
    design, nets, parts = compact_led.build_design()
    logical_before = design.to_json()

    schematic = compact_led.author_schematic(design, nets, parts)
    projection = json.loads(schematic.to_json())
    report = schematic.validate()

    assert design.to_json() == logical_before
    assert len(report) == 0
    assert not report.has_errors
    assert any(
        wire["net"] == f"net:{nets['LED_A'].index}" for wire in projection["wire_runs"]
    )
    assert [port["kind"] for port in projection["power_ports"]] == ["Power", "Ground"]
    assert [field["value"] for field in projection["symbol_fields"]] == [
        "R1",
        "330 ohm",
        "D1",
    ]

    with TemporaryDirectory() as temp_dir:
        first = compact_led.write_artifacts(Path(temp_dir) / "first")
        second = compact_led.write_artifacts(Path(temp_dir) / "second")

        assert first.logical_json.read_text(
            encoding="utf-8"
        ) == second.logical_json.read_text(encoding="utf-8")
        assert first.schematic_json.read_text(
            encoding="utf-8"
        ) == second.schematic_json.read_text(encoding="utf-8")
        svg = first.schematic_svg.read_text(encoding="utf-8")

    assert 'class="wire-run"' in svg
    assert 'class="power-port power"' in svg
    assert 'class="power-port ground"' in svg
    assert ">LED_A</text>" in svg
    assert ">+3V3</text>" in svg
    assert ">GND</text>" in svg
    assert "pin-anchor" not in svg
    assert "pin-label" not in svg
