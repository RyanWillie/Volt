import importlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory



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


def test_regulator_fragment_sugar_example_uses_local_stubs_shapes_and_no_connects():
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
    assert projection["sheet_ports"] == []
    stub_labels = projection["net_labels"][-3:]
    assert [label["net"] for label in stub_labels] == [
        f"net:{nets['NRST'].index}",
        f"net:{nets['SWDIO'].index}",
        f"net:{nets['SWCLK'].index}",
    ]
    assert [label["orientation"] for label in stub_labels] == ["Right", "Right", "Right"]
    no_connect_markers = projection["no_connect_markers"]
    assert len(no_connect_markers) == 1
    assert no_connect_markers[0]["pin"] == f"pin:{parts['TP2']['TP'].index}"
    assert no_connect_markers[0]["reason"] == "reserved pad not populated"
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
    assert 'class="sheet-port off-page"' not in svg
    assert svg.count('class="net-label"') >= 4
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
