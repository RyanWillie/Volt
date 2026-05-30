import json
import math

import pytest

import volt


def _small_resistor_led_design():
    design = volt.Design("pcb-led")

    vcc = design.net("VCC", kind="power")
    led_a = design.net("LED_A")
    gnd = design.net("GND", kind="ground")

    r1 = design.R("330", ref="R1")
    d1 = design.LED(ref="D1")

    vcc += r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"]

    r1.select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-07330RL",
        package="0603",
        footprint=("passives", "R_0603_1608Metric"),
        pin_pads={1: "1", 2: "2"},
    )
    d1.select_part(
        manufacturer="Lite-On",
        part_number="LTST-C190KRKT",
        package="0603",
        footprint=("leds", "LED_0603_1608Metric"),
        pin_pads={"A": "1", "K": "2"},
    )

    return design, r1, d1


def _passive_0603(ref):
    return volt.FootprintDefinition(
        ref,
        pads=(
            volt.FootprintPad.surface_mount(
                "1", at=(-0.75, 0.0), size=(0.80, 0.95), shape="rounded_rectangle"
            ),
            volt.FootprintPad.surface_mount(
                "2", at=(0.75, 0.0), size=(0.80, 0.95), shape="rounded_rectangle"
            ),
        ),
    )


def test_python_board_authoring_writes_deterministic_json_and_svg(tmp_path):
    design, r1, d1 = _small_resistor_led_design()
    board = design.board("Control")

    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
    board.add_mounting_hole("MH1", at=(3.0, 3.0), diameter=3.2)
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    board.cache_footprint(_passive_0603(("leds", "LED_0603_1608Metric")))
    board.place(r1, at=(18.0, 15.0), rotation=0.0, side="top", locked=True)
    board.place(d1, at=(28.0, 15.0), rotation=180.0, side="top")

    first_json = board.to_json()
    assert board.to_json() == first_json
    document = json.loads(first_json)

    assert document["board"]["name"] == "Control"
    assert document["board"]["layers"][0]["name"] == "F.Cu"
    assert document["board"]["layer_stack"]["layers"] == ["board_layer:0", "board_layer:1"]
    assert document["board"]["outline"]["vertices"][2] == [50.0, 30.0]
    assert document["board"]["features"][0]["kind"] == "mounting_hole"
    assert [item["component"] for item in document["board"]["placements"]] == [
        "component:0",
        "component:1",
    ]
    assert len(document["board"]["footprint_definitions"]) == 2
    assert len(document["viewer"]["pad_resolutions"]) == 4
    assert document["viewer"]["diagnostics"] == []

    svg = board.to_svg()
    assert board.to_svg() == svg
    assert 'data-board-name="Control"' in svg
    assert 'data-placement="component_placement:0"' in svg
    assert 'data-net="net:0"' in svg

    json_path = tmp_path / "board.voltpcb.json"
    svg_path = tmp_path / "board.svg"
    board.write_json(json_path)
    board.write_svg(svg_path)
    assert json_path.read_text(encoding="utf-8") == first_json
    assert svg_path.read_text(encoding="utf-8") == svg


def test_python_board_authoring_exposes_pad_resolution_and_validation_diagnostics():
    design, r1, d1 = _small_resistor_led_design()
    c1 = design.C("100nF", ref="C1")
    board = design.board()
    board.set_polygon_outline(((0.0, 0.0), (12.0, 0.0), (12.0, 12.0), (0.0, 12.0)))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    board.place(r1, at=(6.0, 6.0))

    resolutions = board.resolve_pads()
    assert [resolution.status for resolution in resolutions] == ["connected", "connected"]
    assert resolutions[0].component == r1.index
    assert resolutions[0].pin == r1[1].index
    assert resolutions[0].net is not None
    assert resolutions[0].position == (5.25, 6.0)

    report = board.validate()
    assert report.has_errors
    assert {diagnostic.code for diagnostic in report} == {
        "PCB_COMPONENT_NOT_PLACED",
        "PCB_COMPONENT_MISSING_SELECTED_PART",
    }
    assert any(
        entity.kind == "component" and entity.index == d1.index
        for entity in report[0].entities
    )
    assert any(
        entity.kind == "component" and entity.index == c1.index
        for diagnostic in report
        for entity in diagnostic.entities
    )


def test_python_board_authoring_surfaces_kernel_structural_rejections():
    design, r1, _d1 = _small_resistor_led_design()
    board = design.board()
    front = board.add_layer("F.Cu", role="copper", side="top")

    with pytest.raises(RuntimeError, match="Board layer name already exists"):
        board.add_layer("F.Cu", role="copper", side="top")
    with pytest.raises(IndexError, match="Board layer ID does not belong"):
        board.set_layer_stack((front, 99), thickness=1.6)
    with pytest.raises(ValueError, match="Board point coordinates must be finite"):
        board.set_rectangular_outline(origin=(math.nan, 0.0), size=(10.0, 10.0))
    with pytest.raises(RuntimeError, match="Board footprint definition already exists"):
        board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
        board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    with pytest.raises(IndexError, match="Volt entity id is out of range"):
        board.place(99, at=(1.0, 1.0))
    with pytest.raises(RuntimeError, match="Component already has a board placement"):
        board.place(r1, at=(1.0, 1.0))
        board.place(r1, at=(2.0, 2.0))
