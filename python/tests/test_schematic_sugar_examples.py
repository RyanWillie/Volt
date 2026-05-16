import importlib
import json
import sys
from pathlib import Path
from tempfile import TemporaryDirectory


PROJECT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT_ROOT))


def _load_example(name: str):
    return importlib.import_module(f"examples.schematic_sugar.{name}")


def _assert_stable_artifacts(example_module):
    with TemporaryDirectory() as temp_dir:
        first = example_module.write_artifacts(Path(temp_dir) / "first")
        second = example_module.write_artifacts(Path(temp_dir) / "second")

        assert first.logical_json.read_text(encoding="utf-8") == second.logical_json.read_text(
            encoding="utf-8"
        )
        assert first.schematic_json.read_text(
            encoding="utf-8"
        ) == second.schematic_json.read_text(encoding="utf-8")
        assert first.schematic_svg.read_text(
            encoding="utf-8"
        ) == second.schematic_svg.read_text(encoding="utf-8")
        return (
            json.loads(first.logical_json.read_text(encoding="utf-8")),
            json.loads(first.schematic_json.read_text(encoding="utf-8")),
            first.schematic_svg.read_text(encoding="utf-8"),
        )


def test_compact_led_sugar_example_executes_and_renders_readable_schematic():
    compact_led = _load_example("compact_led")
    design, nets, parts = compact_led.build_design()
    logical_before = design.to_json()

    schematic = compact_led.author_schematic(design, nets, parts)
    projection = json.loads(schematic.to_json())
    report = schematic.validate()

    assert design.to_json() == logical_before
    assert len(report) == 0
    assert not report.has_errors
    assert projection["wire_runs"][0]["net"] == f"net:{nets['LED_A'].index}"
    assert [port["kind"] for port in projection["power_ports"]] == ["Power", "Ground"]
    assert [field["value"] for field in projection["symbol_fields"]] == [
        "R1",
        "330 ohm",
        "D1",
    ]

    logical, stable_projection, svg = _assert_stable_artifacts(compact_led)
    assert logical["format"] == "volt.logical_circuit"
    assert stable_projection == projection
    assert "<svg xmlns=\"http://www.w3.org/2000/svg\"" in svg
    assert 'class="wire-run"' in svg
    assert 'class="power-port power"' in svg
    assert 'class="power-port ground"' in svg
    assert ">R1</text>" in svg
    assert ">330 ohm</text>" in svg
    assert ">D1</text>" in svg
    assert ">LED_A</text>" in svg
    assert ">+3V3</text>" in svg
    assert ">GND</text>" in svg
    assert "pin-anchor" not in svg
    assert "pin-label" not in svg


def test_regulator_fragment_sugar_example_uses_ports_shapes_and_no_connects():
    regulator_fragment = _load_example("regulator_fragment")
    design, nets, parts = regulator_fragment.build_design()
    logical_before = design.to_json()

    schematic = regulator_fragment.author_schematic(design, nets, parts)
    projection = json.loads(schematic.to_json())
    report = schematic.validate()

    assert design.to_json() == logical_before
    assert len(report) == 0
    assert not report.has_errors
    assert {wire["route_intent"] for wire in projection["wire_runs"]} == {
        "Direct",
        "Orthogonal",
    }
    assert {port["name"] for port in projection["sheet_ports"]} == {
        "NRST",
        "SWDIO",
        "SWCLK",
    }
    assert projection["no_connect_markers"] == [
        {
            "id": "no_connect_marker:0",
            "sheet": "sheet:0",
            "pin": f"pin:{parts['TP2']['TP'].index}",
            "position": {"x": 258.0, "y": 130.0},
            "orientation": "Right",
            "reason": "reserved pad not populated",
        }
    ]
    assert [field["value"] for field in projection["symbol_fields"]] == [
        "U1",
        "3.3 V regulator",
        "CIN",
        "10 uF",
        "COUT",
        "10 uF",
        "RRESET",
        "10k",
        "TP1",
        "J1",
        "TP2",
    ]

    logical, stable_projection, svg = _assert_stable_artifacts(regulator_fragment)
    assert logical["design_intent"]["stub_nets"] == [
        f"net:{nets['NRST'].index}",
        f"net:{nets['SWDIO'].index}",
        f"net:{nets['SWCLK'].index}",
    ]
    assert logical["design_intent"]["no_connect_pins"] == [
        f"pin:{parts['TP2']['TP'].index}"
    ]
    assert stable_projection == projection
    assert 'class="sheet-port off-page"' in svg
    assert 'class="no-connect-marker"' in svg
    assert 'class="power-port power"' in svg
    assert 'class="power-port ground"' in svg
    assert ">U1</text>" in svg
    assert ">3.3 V regulator</text>" in svg
    assert ">CIN</text>" in svg
    assert ">10 uF</text>" in svg
    assert ">NRST</text>" in svg
    assert ">SWDIO</text>" in svg
    assert ">SWCLK</text>" in svg
    assert "pin-anchor" not in svg
    assert "pin-label" not in svg


if __name__ == "__main__":
    test_compact_led_sugar_example_executes_and_renders_readable_schematic()
    test_regulator_fragment_sugar_example_uses_ports_shapes_and_no_connects()
