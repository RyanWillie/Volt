import json
import math
from pathlib import Path

import pytest

import volt

ROOT = Path(__file__).resolve().parents[2]


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


def _passive_0603(ref, *, pad_span=1.5, pad_width=0.8):
    half_span = pad_span / 2
    return volt.FootprintDefinition(
        ref,
        pads=(
            volt.FootprintPad.surface_mount(
                "1",
                at=(-half_span, 0.0),
                size=(pad_width, 0.95),
                shape="rounded_rectangle",
            ),
            volt.FootprintPad.surface_mount(
                "2",
                at=(half_span, 0.0),
                size=(pad_width, 0.95),
                shape="rounded_rectangle",
            ),
        ),
    )


def _two_pad_footprint(ref, *, start=(0.0, 0.0), end=(1.5, 0.0)):
    return volt.FootprintDefinition(
        ref,
        pads=(
            volt.FootprintPad.surface_mount(
                "1",
                at=start,
                size=(0.8, 0.95),
                shape="rounded_rectangle",
            ),
            volt.FootprintPad.surface_mount(
                "2",
                at=end,
                size=(0.8, 0.95),
                shape="rounded_rectangle",
            ),
        ),
    )


def _placed_positions(board):
    return {
        item["component"]: (tuple(item["position"]), item["rotation_deg"], item["locked"])
        for item in json.loads(board.to_json())["board"]["placements"]
    }


def _fixture_path(name: str) -> Path:
    return ROOT / "tests" / "fixtures" / name


def test_pcb_layout_session_defaults_and_moves():
    design = volt.Design("pcb-layout-session")
    board = design.board("Control")
    board.set_rectangular_outline(origin=(2.0, 3.0), size=(30.0, 20.0))

    with board.layout(at=(1, 2), direction="down", unit=2.5) as layout:
        assert isinstance(layout.here, volt.BoardAnchor)
        assert layout.here.point == (1.0, 2.0)
        assert layout.direction == "Down"
        assert layout.unit == 2.5

        layout.move(dx=3, dy=4)
        assert layout.here.point == (4.0, 6.0)

        layout.move_from(board.edge("left").center().right(5), dy=-1, direction="right")
        assert layout.here.point == (7.0, 12.0)
        assert layout.direction == "Right"

        row = layout.stack(count=3, direction="Right", pitch=4)
        column = layout.stack(count=2, direction="Down")

        with layout.hold():
            layout.move_from(row[-1], direction="Left")
            assert layout.here.point == (15.0, 12.0)
            assert layout.direction == "Left"

        assert layout.here.point == (7.0, 12.0)
        assert layout.direction == "Right"

        with layout.frame(board.corner("top-left").right(10).down(5), direction="Down"):
            assert layout.here.point == (12.0, 8.0)
            assert layout.node((1, 2)).point == (13.0, 10.0)
            assert layout.stack(count=2, direction="Down", pitch=3)[1].point == (12.0, 11.0)

        assert [anchor.point for anchor in row] == [
            (7.0, 12.0),
            (11.0, 12.0),
            (15.0, 12.0),
        ]
        assert [anchor.point for anchor in column] == [(7.0, 12.0), (7.0, 14.5)]

    assert board.center.point == (17.0, 13.0)
    assert board.edge("right").center().point == (32.0, 13.0)
    assert board.corner("bottom-right").point == (32.0, 23.0)


def test_pcb_layout_place_returns_placed_component_handle():
    design, r1, d1 = _small_resistor_led_design()
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    board.cache_footprint(_passive_0603(("leds", "LED_0603_1608Metric")))

    with board.layout(unit=1.0) as layout:
        resistor = layout.place(
            r1,
            at=board.edge("left").center().right(18),
            orient="right",
            locked=True,
        )
        led = layout.place(d1, at=resistor.center.right(10), orient="left")

    assert isinstance(resistor, volt.PlacedBoardComponent)
    assert resistor.index == 0
    assert resistor.component is r1
    assert resistor.center.point == (18.0, 15.0)
    assert resistor.pad("1").point == (17.25, 15.0)
    assert resistor.pad("2").point == (18.75, 15.0)
    assert resistor[1].point == resistor.pad("1").point
    assert resistor[2].point == resistor.pad("2").point
    assert led.A.point == (28.75, 15.0)
    assert led.K.point == (27.25, 15.0)

    assert _placed_positions(board) == {
        "component:0": ((18.0, 15.0), 0, True),
        "component:1": ((28.0, 15.0), 180, False),
    }
    assert board.to_json() == board.to_json()


def test_pcb_layout_two_pad_right_left_up_down_places_resolved_coordinates():
    design = volt.Design("pcb-layout-two-pad")
    parts = [design.R(f"{value}k", ref=f"R{value}") for value in range(1, 5)]
    for index, component in enumerate(parts, start=1):
        left = design.net(f"LEFT{index}")
        right = design.net(f"RIGHT{index}")
        left += component[1]
        right += component[2]
        component.select_part(
            manufacturer="Yageo",
            part_number=f"RC0603-{index}",
            package="0603",
            footprint=_passive_0603(("passives", f"R_0603_{index}")),
            pin_pads={1: "1", 2: "2"},
        )
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(40.0, 30.0))

    with board.layout(unit=1.0) as layout:
        right = layout.two_pad(parts[0]).at((5.0, 5.0)).right()
        down = layout.two_pad(parts[1]).at(right.end.down(4)).down()
        left = layout.two_pad(parts[2]).at((20.0, 5.0)).left()
        up = layout.two_pad(parts[3]).at((25.0, 15.0)).up()

    assert right.start.point == (5.0, 5.0)
    assert right.end.point == (6.5, 5.0)
    assert right.center.point == (5.75, 5.0)
    assert down.start.point == (6.5, 9.0)
    assert down.end.point == (6.5, 10.5)
    assert left.start.point == (20.0, 5.0)
    assert left.end.point == (18.5, 5.0)
    assert up.start.point == (25.0, 15.0)
    assert up.end.point == (25.0, 13.5)
    assert _placed_positions(board) == {
        "component:0": ((5.75, 5.0), 0, False),
        "component:1": ((6.5, 9.75), 90, False),
        "component:2": ((19.25, 5.0), 180, False),
        "component:3": ((25.0, 14.25), 270, False),
    }


def test_pcb_layout_two_pad_directions_follow_actual_pad_vector():
    design = volt.Design("pcb-layout-two-pad-vector")
    parts = [design.R(f"{value}k", ref=f"R{value}") for value in range(1, 5)]
    footprints = (
        _two_pad_footprint(("test", "vertical"), end=(0.0, 1.5)),
        _two_pad_footprint(("test", "reversed"), end=(-1.5, 0.0)),
        _two_pad_footprint(("test", "vertical-left"), end=(0.0, 1.5)),
        _two_pad_footprint(("test", "reversed-up"), end=(-1.5, 0.0)),
    )
    for index, component in enumerate(parts):
        net_a = design.net(f"A{index}")
        net_b = design.net(f"B{index}")
        net_a += component[1]
        net_b += component[2]
        component.select_part(
            manufacturer="Volt",
            part_number=f"VECTOR-{index}",
            package="custom",
            footprint=footprints[index],
            pin_pads={1: "1", 2: "2"},
        )
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(40.0, 30.0))

    with board.layout(unit=1.0) as layout:
        vertical_right = layout.two_pad(parts[0]).at((5.0, 5.0)).right()
        reversed_down = layout.two_pad(parts[1]).at((10.0, 5.0)).down()
        vertical_left = layout.two_pad(parts[2]).at((20.0, 5.0)).left()
        reversed_up = layout.two_pad(parts[3]).at((25.0, 15.0)).up()

    assert vertical_right.start.point == (5.0, 5.0)
    assert vertical_right.end.point == (6.5, 5.0)
    assert reversed_down.start.point == (10.0, 5.0)
    assert reversed_down.end.point == (10.0, 6.5)
    assert vertical_left.start.point == (20.0, 5.0)
    assert vertical_left.end.point == (18.5, 5.0)
    assert reversed_up.start.point == (25.0, 15.0)
    assert reversed_up.end.point == (25.0, 13.5)
    assert _placed_positions(board) == {
        "component:0": ((5.0, 5.0), 270, False),
        "component:1": ((10.0, 5.0), 270, False),
        "component:2": ((20.0, 5.0), 90, False),
        "component:3": ((25.0, 15.0), 90, False),
    }


def test_pcb_layout_two_pad_uses_kernel_resolved_builtin_footprints():
    design, r1, _d1 = _small_resistor_led_design()
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))

    with board.layout(unit=1.0) as layout:
        resistor = layout.two_pad(r1).at((5.0, 5.0)).right()

    assert isinstance(resistor, volt.PlacedBoardComponent)
    assert resistor.start.point == (5.0, 5.0)
    assert resistor.end.point == (6.5, 5.0)
    assert _placed_positions(board) == {
        "component:0": ((5.75, 5.0), 0, False),
    }


def test_pcb_layout_two_pad_builder_does_not_flush_from_layout_operations():
    design = volt.Design("pcb-layout-two-pad-builder")
    r1 = design.R("1k", ref="R1")
    net_a = design.net("A")
    net_b = design.net("B")
    net_a += r1[1]
    net_b += r1[2]
    r1.select_part(
        manufacturer="Yageo",
        part_number="RC0603",
        package="0603",
        footprint=_passive_0603(("passives", "R_0603")),
        pin_pads={1: "1", 2: "2"},
    )
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))

    with board.layout(unit=1.0) as layout:
        builder = layout.two_pad(r1).at((5.0, 5.0))
        layout.move(dx=4.0)
        assert board.resolve_pads() == ()
        placed = builder.right()

    assert placed.start.point == (5.0, 5.0)
    assert placed.end.point == (6.5, 5.0)
    assert _placed_positions(board) == {
        "component:0": ((5.75, 5.0), 0, False),
    }
    assert layout.here.point == (6.5, 5.0)


def test_pcb_layout_routes_tracks_and_vias_from_relative_anchors():
    design, r1, d1 = _small_resistor_led_design()
    led_a = next(net for net in design.nets() if net.name == "LED_A")
    board = design.board("Control")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    board.cache_footprint(_passive_0603(("leds", "LED_0603_1608Metric")))

    with board.layout(unit=1.0) as layout:
        resistor = layout.two_pad(r1).at((10.0, 10.0)).right()
        led = layout.place(d1, at=resistor.center.right(12).down(5), orient="left")

        front_track = (
            layout.route(led_a, layer=front, width=0.25)
            .at(resistor.end)
            .right(2.0)
            .toy(led.A)
            .to(led.A)
        )
        via_anchor = layout.node(led.K.left(2.0))
        via = layout.via(led_a, at=via_anchor, start_layer=front, end_layer=back)
        back_track = layout.route(led_a, layer=back, width=0.30).to(
            via_anchor.right(3.0)
        )

    document = json.loads(board.to_json())
    assert isinstance(layout.here, volt.BoardAnchor)
    assert layout.here.point == (23.0, 15.0)
    assert front_track == 0
    assert via == 0
    assert back_track == 1
    assert document["board"]["tracks"][0]["points"] == [
        [11.5, 10.0],
        [13.5, 10.0],
        [13.5, 15.0],
        [23.5, 15.0],
    ]
    assert document["board"]["tracks"][0]["width_mm"] == 0.25
    assert document["board"]["tracks"][1]["points"] == [[20.0, 15.0], [23.0, 15.0]]
    assert document["board"]["tracks"][1]["layer"] == "board_layer:1"
    assert document["board"]["tracks"][1]["width_mm"] == 0.30
    assert document["board"]["vias"][0]["position"] == [20.0, 15.0]
    assert document["board"]["vias"][0]["start_layer"] == "board_layer:0"
    assert document["board"]["vias"][0]["end_layer"] == "board_layer:1"


def test_pcb_layout_grid_snaps_explicit_authoring_coordinates():
    design, r1, _d1 = _small_resistor_led_design()
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(30.0, 20.0))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))

    with board.layout(at=(0.26, 0.74), grid=0.5) as layout:
        assert layout.grid == 0.5
        assert layout.here.point == (0.5, 0.5)
        assert layout.snap((10.26, 5.74)).point == (10.5, 5.5)
        assert layout.snap_x(10.26) == 10.5
        assert layout.snap_y(5.74) == 5.5
        assert layout.snap_x(0.25) == 0.5
        assert layout.snap_x(-0.25) == -0.5

        resistor = layout.place(r1, at=(10.26, 5.74), orient="right")
        with layout.frame((2.26, 3.24)):
            assert layout.node((1.26, 1.26)).point == (4.0, 4.5)

    assert resistor.center.point == (10.5, 5.5)
    assert _placed_positions(board) == {
        "component:0": ((10.5, 5.5), 0, False),
    }


def test_pcb_layout_routes_default_to_octilinear_segments():
    design = volt.Design("pcb-octilinear-routes")
    net = design.net("SIG")
    board = design.board("Control")
    layer = board.add_layer("F.Cu", role="copper", side="top")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))

    with board.layout() as layout:
        arbitrary = layout.route(net, layer=layer).at((1.0, 1.0)).to((5.0, 2.0))
        diagonal = layout.route(net, layer=layer).at((1.0, 4.0)).to((3.0, 6.0))
        direct = (
            layout.route(net, layer=layer)
            .at((1.0, 8.0))
            .to((5.0, 9.0), mode="direct")
        )

    document = json.loads(board.to_json())
    assert arbitrary == 0
    assert diagonal == 1
    assert direct == 2
    assert document["board"]["tracks"][0]["points"] == [[1, 1], [5, 1], [5, 2]]
    assert document["board"]["tracks"][1]["points"] == [[1, 4], [3, 6]]
    assert document["board"]["tracks"][2]["points"] == [[1, 8], [5, 9]]


def test_pcb_layout_grid_snaps_route_numeric_helpers_without_snapping_anchor_targets():
    design, r1, _d1 = _small_resistor_led_design()
    net = design.net("SIG")
    board = design.board("Control")
    layer = board.add_layer("F.Cu", role="copper", side="top")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))

    with board.layout(grid=0.5) as layout:
        helper = (
            layout.route(net, layer=layer)
            .at((1.1, 1.1))
            .tox(2.26)
            .toy(3.24)
            .to((4.26, 3.24))
        )
        jog = (
            layout.route(net, layer=layer)
            .at((1.1, 6.1))
            .right(1.26)
            .down(1.26)
            .to((4.26, 7.26))
        )
        resistor = layout.two_pad(r1).at((8.0, 5.0)).right()
        anchor_target = resistor.end.right(1.26)
        exact_anchor = (
            layout.route(net, layer=layer)
            .at(resistor.end)
            .tox(anchor_target)
            .to(anchor_target)
        )

    document = json.loads(board.to_json())
    assert helper == 0
    assert jog == 1
    assert exact_anchor == 2
    assert document["board"]["tracks"][0]["points"] == [
        [1, 1],
        [2.5, 1],
        [2.5, 3],
        [4.5, 3],
    ]
    assert document["board"]["tracks"][1]["points"] == [
        [1, 6],
        [2.5, 6],
        [2.5, 7.5],
        [4.5, 7.5],
    ]
    assert document["board"]["tracks"][2]["points"] == [[9.5, 5], [10.76, 5]]


def test_pcb_layout_composes_generic_anchor_sets():
    design = volt.Design("pcb-layout-anchor-composition")
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(30.0, 20.0))

    with board.layout(grid=0.5) as layout:
        aligned = layout.align(
            (layout.node((2.1, 3.2)), layout.node((8.2, 6.7))),
            axis="y",
            target=board.center,
        )
        distributed = layout.distribute(count=3, start=(2.0, 2.0), end=(10.0, 6.0))
        mirrored = layout.mirror((layout.node((4.0, 5.0)),), axis="x", about=board.center)

    assert [anchor.point for anchor in aligned] == [(2.0, 10.0), (8.0, 10.0)]
    assert [anchor.point for anchor in distributed] == [
        (2.0, 2.0),
        (6.0, 4.0),
        (10.0, 6.0),
    ]
    assert [anchor.point for anchor in mirrored] == [(26.0, 5.0)]


def test_pcb_layout_connects_pads_through_intermediate_anchors_with_rule_width():
    design, r1, d1 = _small_resistor_led_design()
    led_a = next(net for net in design.nets() if net.name == "LED_A")
    board = design.board("Control")
    front = board.add_layer("F.Cu", role="copper", side="top")
    board.set_design_rules(min_track_width=0.25)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(40.0, 24.0))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    board.cache_footprint(_passive_0603(("leds", "LED_0603_1608Metric")))

    with board.layout(unit=1.0) as layout:
        resistor = layout.two_pad(r1).at((10.0, 10.0)).right()
        led = layout.place(d1, at=resistor.center.right(12).down(5), orient="left")
        assert layout.rule("min_track_width") == 0.25

        track = layout.connect(
            resistor.end,
            led.A,
            layer=front,
            width=layout.rule("min_track_width"),
            through=(resistor.end.right(2.0),),
        )

    document = json.loads(board.to_json())
    assert track == 0
    assert document["board"]["tracks"][0]["net"] == "net:1"
    assert document["board"]["tracks"][0]["width_mm"] == 0.25
    assert document["board"]["tracks"][0]["points"] == [
        [11.5, 10.0],
        [13.5, 10.0],
        [23.5, 10.0],
        [23.5, 15.0],
    ]


def test_pcb_layout_bundles_independent_routes_with_net_inference():
    design = volt.Design("pcb-layout-route-bundle")
    net_a = design.net("A")
    net_b = design.net("B")
    left_component = design.R("1k", ref="R1")
    right_component = design.R("1k", ref="R2")
    net_a += left_component[1], right_component[1]
    net_b += left_component[2], right_component[2]
    for component in (left_component, right_component):
        component.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-071KL",
            package="0603",
            footprint=("passives", "R_0603_1608Metric"),
            pin_pads={1: "1", 2: "2"},
        )

    board = design.board("Control")
    front = board.add_layer("F.Cu", role="copper", side="top")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(32.0, 20.0))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))

    with board.layout(unit=1.0) as layout:
        left = layout.two_pad(left_component).at((10.0, 8.0)).right()
        right = layout.two_pad(right_component).at((20.0, 12.0)).right()
        cursor = layout.here.point
        tracks = layout.bundle(
            (
                (left[1], right[1]),
                (left[2], right[2], (left[2].right(2.0),)),
            ),
            layer=front,
            width=0.25,
        )
        assert layout.here.point == cursor

    document = json.loads(board.to_json())
    assert tracks == (0, 1)
    assert [track["net"] for track in document["board"]["tracks"]] == ["net:0", "net:1"]
    assert [track["width_mm"] for track in document["board"]["tracks"]] == [0.25, 0.25]
    assert document["board"]["tracks"][0]["points"] == [
        [10.0, 8.0],
        [20.0, 8.0],
        [20.0, 12.0],
    ]
    assert document["board"]["tracks"][1]["points"] == [
        [11.5, 8.0],
        [13.5, 8.0],
        [21.5, 8.0],
        [21.5, 12.0],
    ]


def test_pcb_layout_connect_re_resolves_pad_anchor_nets_at_mutation_time():
    design = volt.Design("pcb-layout-live-net-inference")
    left_component = design.R("1k", ref="R1")
    right_component = design.R("1k", ref="R2")
    for component in (left_component, right_component):
        component.select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-071KL",
            package="0603",
            footprint=("passives", "R_0603_1608Metric"),
            pin_pads={1: "1", 2: "2"},
        )

    board = design.board("Control")
    front = board.add_layer("F.Cu", role="copper", side="top")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(32.0, 20.0))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))

    with board.layout(unit=1.0) as layout:
        left = layout.two_pad(left_component).at((10.0, 8.0)).right()
        right = layout.two_pad(right_component).at((20.0, 8.0)).right()
        left_anchor = left[1]
        right_anchor = right[1]

        late_net = design.net("LATE")
        late_net += left_component[1], right_component[1]

        track = layout.connect(left_anchor, right_anchor, layer=front, mode="direct")

    document = json.loads(board.to_json())
    assert track == 0
    assert document["board"]["tracks"][0]["net"] == "net:0"
    assert document["board"]["tracks"][0]["points"] == [[10.0, 8.0], [20.0, 8.0]]


def test_pcb_layout_fanout_and_stitch_lower_to_tracks_and_vias():
    design, _r1, d1 = _small_resistor_led_design()
    gnd = next(net for net in design.nets() if net.name == "GND")
    board = design.board("Control")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(30.0, 20.0))
    board.cache_footprint(_passive_0603(("leds", "LED_0603_1608Metric")))

    with board.layout(grid=0.5) as layout:
        led = layout.place(d1, at=(12.0, 10.0), orient="right")
        cursor = layout.here.point
        fanouts = layout.fanout(
            (led.K,),
            layer=front,
            direction="left",
            distance=2.0,
            via_layers=(front, back),
        )
        stitched = layout.stitch(
            gnd,
            at=(
                board.corner("top-left").right(4).down(4),
                board.corner("bottom-left").right(4).up(4),
            ),
            start_layer=front,
            end_layer=back,
        )
        assert layout.here.point == cursor

    document = json.loads(board.to_json())
    assert len(fanouts) == 1
    assert fanouts[0].track == 0
    assert fanouts[0].via == 0
    assert fanouts[0].end.point == (10.75, 10.0)
    assert stitched == (1, 2)
    assert document["board"]["tracks"][0]["points"] == [[12.75, 10.0], [10.75, 10.0]]
    assert [via["position"] for via in document["board"]["vias"]] == [
        [10.75, 10.0],
        [4.0, 4.0],
        [4.0, 16.0],
    ]


def test_pcb_layout_board_anchors_read_outline_without_serializing(monkeypatch):
    design = volt.Design("pcb-layout-outline-query")
    board = design.board("Control")
    board.set_rectangular_outline(origin=(2.0, 3.0), size=(30.0, 20.0))

    def fail_to_json():
        raise AssertionError("board anchors should not serialize PCB JSON")

    monkeypatch.setattr(board, "to_json", fail_to_json)

    assert board.center.point == (17.0, 13.0)
    assert board.edge("right").center().point == (32.0, 13.0)
    assert board.corner("bottom-left").point == (2.0, 23.0)


def test_pcb_layout_frame_and_json_match_absolute_placement_equivalent():
    relative_design, relative_r1, relative_d1 = _small_resistor_led_design()
    relative_board = relative_design.board("Control")
    relative_board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
    relative_board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    relative_board.cache_footprint(_passive_0603(("leds", "LED_0603_1608Metric")))

    with relative_board.layout(unit=1.0) as layout:
        with layout.frame((10.0, 5.0), direction="Right"):
            anchors = layout.stack(count=2, direction="Right", pitch=10)
            layout.place(relative_r1, at=anchors[0], orient="right", locked=True)
            layout.place(relative_d1, at=anchors[1], orient="left")

    absolute_design, absolute_r1, absolute_d1 = _small_resistor_led_design()
    absolute_board = absolute_design.board("Control")
    absolute_board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
    absolute_board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    absolute_board.cache_footprint(_passive_0603(("leds", "LED_0603_1608Metric")))
    absolute_board.place(absolute_r1, at=(10.0, 5.0), rotation=0.0, locked=True)
    absolute_board.place(absolute_d1, at=(20.0, 5.0), rotation=180.0)

    assert _placed_positions(relative_board) == _placed_positions(absolute_board)


def test_pcb_layout_reports_invalid_components_and_ambiguous_pin_names():
    design = volt.Design("pcb-layout-errors")
    duplicate = design.define_component(
        "DuplicatePins",
        pins=[volt.PinSpec("IO", 1), volt.PinSpec("IO", 2)],
    )
    u1 = design.instantiate(duplicate, ref="U1")
    u1.select_part(
        manufacturer="Volt",
        part_number="DUP",
        package="0603",
        footprint=_passive_0603(("volt.test", "DuplicatePins")),
        pin_pads={1: "1", 2: "2"},
    )
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))

    with board.layout() as layout:
        placed = layout.place(u1, at=(10.0, 6.0))

    with pytest.raises(AttributeError, match="ambiguous"):
        placed.IO
    with pytest.raises(ValueError, match="requires exactly two component pins"):
        layout.two_pad(design.test_point(ref="TP1"))

    other_design = volt.Design("other")
    other_board = other_design.board("Other")
    other_board.set_rectangular_outline(origin=(0.0, 0.0), size=(10.0, 10.0))
    with pytest.raises(ValueError, match="different board"):
        layout.move_from(other_board.center)

    missing = design.R("1k", ref="R1")
    missing.select_part(
        manufacturer="Yageo",
        part_number="RC0603",
        package="0603",
        footprint=("missing", "R_0603_1608Metric"),
        pin_pads={1: "1", 2: "2"},
    )
    with pytest.raises(ValueError, match="resolved footprint pad geometry"):
        layout.two_pad(missing).right()


def test_python_board_authoring_writes_deterministic_json_and_svg(tmp_path):
    design, r1, d1 = _small_resistor_led_design()
    led_a = next(net for net in design.nets() if net.name == "LED_A")
    board = design.board("Control")

    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
    board.add(volt.Hole(center=(3.0, 3.0), diameter=3.2, role="mounting", label="MH1"))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    board.cache_footprint(_passive_0603(("leds", "LED_0603_1608Metric")))
    board.place(r1, at=(18.0, 15.0), rotation=0.0, side="top", locked=True)
    board.place(d1, at=(28.0, 15.0), rotation=180.0, side="top")
    track = board.add_track(
        led_a,
        layer=front,
        points=((18.75, 15.0), (23.0, 10.0), (28.75, 15.0)),
        width=0.25,
    )
    via = board.add_via(
        led_a,
        at=(23.0, 15.0),
        start_layer=front,
        end_layer=back,
        drill=0.30,
        annular=0.70,
    )

    first_json = board.to_json()
    assert board.to_json() == first_json
    document = json.loads(first_json)

    assert document["board"]["name"] == "Control"
    assert document["board"]["layers"][0]["name"] == "F.Cu"
    assert document["board"]["layer_stack"]["layers"] == ["board_layer:0", "board_layer:1"]
    assert document["board"]["outline"]["vertices"][2] == [50.0, 30.0]
    assert document["board"]["geometry"]["thickness_mm"] == 1.6
    assert document["board"]["geometry"]["stackup"][0]["z_mm"] == 0.8
    assert document["board"]["geometry"]["openings"][0]["kind"] == "hole"
    assert document["board"]["geometry"]["openings"][0]["side"] == "through_board"
    assert document["board"]["features"][0]["kind"] == "hole"
    assert document["board"]["features"][0]["role"] == "mounting"
    assert [item["component"] for item in document["board"]["placements"]] == [
        "component:0",
        "component:1",
    ]
    assert track == 0
    assert via == 0
    assert document["board"]["tracks"][0]["net"] == "net:1"
    assert document["board"]["tracks"][0]["layer"] == "board_layer:0"
    assert document["board"]["rules"]["minimum_track_width_mm"] == 0.15
    assert document["board"]["tracks"][0]["points"] == [
        [18.75, 15.0],
        [23.0, 10.0],
        [28.75, 15.0],
    ]
    assert document["board"]["vias"][0]["net"] == "net:1"
    assert document["board"]["vias"][0]["start_layer"] == "board_layer:0"
    assert document["board"]["vias"][0]["end_layer"] == "board_layer:1"
    assert len(document["board"]["footprint_definitions"]) == 2
    assert len(document["viewer"]["pad_resolutions"]) == 4
    assert document["viewer"]["diagnostics"] == []

    svg = board.to_svg()
    assert board.to_svg() == svg
    assert 'data-board-name="Control"' in svg
    assert 'data-placement="component_placement:0"' in svg
    assert 'data-net="net:0"' in svg
    assert 'data-track="board_track:0"' in svg
    assert 'data-via="board_via:0"' in svg
    assert 'id="pcb-layer-F_Cu"' in svg
    assert 'id="pcb-layer-B_Cu"' in svg
    assert "data-ratsnest-edge=" in svg
    assert "data-ratsnest-edge=" not in board.to_svg(ratsnest_edges=False)
    back_svg = board.to_svg(layer=back)
    assert 'id="pcb-layer-B_Cu"' in back_svg
    assert 'id="pcb-layer-F_Cu"' not in back_svg
    assert 'data-track="board_track:0"' not in back_svg
    assert 'data-via="board_via:0"' in back_svg

    json_path = tmp_path / "board.voltpcb.json"
    svg_path = tmp_path / "board.svg"
    board.write_json(json_path)
    board.write_svg(svg_path)
    assert json_path.read_text(encoding="utf-8") == first_json
    assert svg_path.read_text(encoding="utf-8") == svg


def test_python_board_authoring_assisted_connect_surfaces_kernel_result():
    design = volt.Design("assisted-connect")
    route = design.net("ROUTE")
    board = design.board("Control")
    front = board.add_layer("F.Cu", role="copper", side="top")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))

    result = board.assisted_connect(
        route,
        start=(2.0, 6.0),
        start_layer=front,
        end=(18.0, 6.0),
        end_layer=front,
    )

    assert result == {"routed": True, "tracks": [0], "vias": [], "blockers": []}
    document = json.loads(board.to_json())
    assert document["board"]["tracks"][0]["net"] == "net:0"
    assert document["board"]["tracks"][0]["layer"] == "board_layer:0"
    assert document["board"]["tracks"][0]["points"] == [[2.0, 6.0], [18.0, 6.0]]

    blocked = volt.Design("blocked-assisted-connect")
    blocked_net = blocked.net("ROUTE")
    blocked_board = blocked.board("Control")
    blocked_front = blocked_board.add_layer("F.Cu", role="copper", side="top")
    blocked_board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
    keepout = blocked_board.add(
        volt.MechanicalKeepout(
            outline=((1.0, 1.0), (19.0, 1.0), (19.0, 11.0), (1.0, 11.0)),
            layers=(blocked_front,),
            restrictions=("copper",),
        )
    )

    failure = blocked_board.assisted_connect(
        blocked_net,
        start=(2.0, 6.0),
        start_layer=blocked_front,
        end=(18.0, 6.0),
        end_layer=blocked_front,
    )

    assert failure["routed"] is False
    assert failure["tracks"] == []
    assert failure["vias"] == []
    assert failure["blockers"] == [
        {
            "kind": "keepout",
            "shape_index": None,
            "keepout": keepout,
            "layer": blocked_front,
            "required_clearance_mm": 0.0,
            "actual_clearance_mm": 0.0,
            "room": None,
        }
    ]
    assert json.loads(blocked_board.to_json())["board"].get("tracks", []) == []


def test_python_board_authoring_escape_surfaces_kernel_result():
    design, r1, _ = _small_resistor_led_design()
    board = design.board("Control")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 20.0))
    board.set_design_rules(copper_clearance=0.20, min_track_width=0.20)
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    board.place(r1, at=(10.0, 10.0))

    result = board.escape(r1)

    assert result["complete"] is True
    assert result["component"] == r1.index
    assert result["placement"] == 0
    assert result["room"] == 0
    assert result["pads"] == [
        {
            "pad": 0,
            "pad_label": "1",
            "pin": r1[1].index,
            "net": 0,
            "pad_position": (9.25, 10.0),
            "endpoint": (8.25, 10.0),
            "escaped": True,
            "failure_reason": "none",
            "tracks": [0],
            "vias": [],
            "blockers": [],
        },
        {
            "pad": 1,
            "pad_label": "2",
            "pin": r1[2].index,
            "net": 1,
            "pad_position": (10.75, 10.0),
            "endpoint": (11.75, 10.0),
            "escaped": True,
            "failure_reason": "none",
            "tracks": [1],
            "vias": [],
            "blockers": [],
        },
    ]

    first_json = board.to_json()
    assert board.to_json() == first_json
    document = json.loads(first_json)
    assert document["board"]["rooms"][0]["name"] == "escape-R1-at-10.000-10.000"
    assert document["board"]["rooms"][0]["layers"] == ["board_layer:0"]
    assert document["board"]["tracks"][0]["points"] == [[9.25, 10.0], [8.25, 10.0]]
    assert document["board"]["tracks"][1]["points"] == [[10.75, 10.0], [11.75, 10.0]]

    blocked = volt.Design("blocked-escape")
    blocked_route = blocked.net("ROUTE")
    blocked_r = blocked.R("1k", ref="R1")
    blocked_route += blocked_r[1], blocked_r[2]
    blocked_r.select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-071KL",
        package="0603",
        footprint=("passives", "R_0603_1608Metric"),
        pin_pads={1: "1", 2: "2"},
    )
    blocked_board = blocked.board("Control")
    blocked_front = blocked_board.add_layer("F.Cu", role="copper", side="top")
    blocked_board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 20.0))
    blocked_board.set_design_rules(copper_clearance=0.20, min_track_width=0.20)
    blocked_board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    blocked_board.place(blocked_r, at=(10.0, 10.0))
    keepout = blocked_board.add_keepout(
        outline=((8.6, 9.5), (9.6, 9.5), (9.6, 10.5), (8.6, 10.5)),
        layers=(blocked_front,),
        restrictions=("copper",),
    )

    failure = blocked_board.escape(blocked_r)

    assert failure["complete"] is False
    assert failure["room"] == 0
    assert failure["pads"][0]["escaped"] is False
    assert failure["pads"][0]["failure_reason"] == "no_legal_candidate"
    assert failure["pads"][0]["tracks"] == []
    assert failure["pads"][0]["blockers"] == [
        {
            "kind": "keepout",
            "shape_index": None,
            "keepout": keepout,
            "layer": blocked_front,
            "required_clearance_mm": 0.0,
            "actual_clearance_mm": 0.0,
            "room": None,
        }
    ]
    assert failure["pads"][1]["escaped"] is True
    assert json.loads(blocked_board.to_json())["board"]["tracks"][0]["points"] == [
        [10.75, 10.0],
        [11.75, 10.0],
    ]


def test_python_board_authoring_exports_kicad_pcb_with_loss_report(tmp_path):
    design, r1, d1 = _small_resistor_led_design()
    led_a = next(net for net in design.nets() if net.name == "LED_A")
    board = design.board("Control")

    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
    board.add(volt.Hole(center=(3.0, 3.0), diameter=3.2, role="mounting", label="MH1"))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    board.cache_footprint(_passive_0603(("leds", "LED_0603_1608Metric")))
    board.place(r1, at=(18.0, 15.0), rotation=0.0, side="top", locked=True)
    board.place(d1, at=(28.0, 15.0), rotation=180.0, side="top")
    board.add_track(
        led_a,
        layer=front,
        points=((18.75, 15.0), (23.0, 10.0), (28.75, 15.0)),
        width=0.25,
    )

    export = board.to_kicad_pcb()

    assert export.warnings == ()
    assert export.text == board.to_kicad_pcb().text
    assert export.text.startswith("(kicad_pcb\n")
    assert '(generator "Volt")' in export.text
    assert '(net 1 "VCC")' in export.text
    assert '(net 2 "LED_A")' in export.text
    assert '(footprint "R_0603_1608Metric"' in export.text
    assert '(49 "F.Fab" user)' in export.text
    assert '(segment\n    (start 18.75 15)' in export.text

    kicad_path = tmp_path / "board.kicad_pcb"
    written = board.write_kicad_pcb(kicad_path)
    assert written.text == export.text
    assert kicad_path.read_text(encoding="utf-8") == export.text

    board.add_zone(
        outline=((2.0, 2.0), (12.0, 2.0), (12.0, 8.0), (2.0, 8.0)),
        layers=(front,),
        net=led_a,
    )
    with_loss = board.to_kicad_pcb()
    assert [warning.construct for warning in with_loss.warnings] == ["board.zone"]
    assert with_loss.warnings[0].kind == "unsupported"
    assert with_loss.warnings[0].severity == "warning"


def test_python_board_authoring_writes_zones_keepouts_rooms_and_text():
    design, r1, _d1 = _small_resistor_led_design()
    led_a = next(net for net in design.nets() if net.name == "LED_A")
    board = design.board("Control")
    front = board.add_layer("F.Cu", role="copper", side="top")
    silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(30.0, 20.0))
    board.place(r1, at=(10.0, 10.0))

    zone = board.add_zone(
        outline=((2.0, 2.0), (10.0, 2.0), (10.0, 7.0), (2.0, 7.0)),
        layers=(front,),
        net=led_a,
        priority=4,
    )
    keepout = board.add(
        volt.MechanicalKeepout(
            outline=((12.0, 2.0), (16.0, 2.0), (16.0, 6.0), (12.0, 6.0)),
            layers=(front,),
            restrictions=("copper", "via"),
        )
    )
    room = board.add_room(
        "BGA escape",
        outline=((18.0, 2.0), (24.0, 2.0), (24.0, 8.0), (18.0, 8.0)),
        layers=(front,),
        clearance=0.075,
        track_width=0.10,
        priority=3,
    )
    text = board.add(
        volt.Text("REV A", at=(5.0, 15.0), layer=silk, rotation=90.0, size=1.2, locked=True)
    )

    document = json.loads(board.to_json())
    assert zone == 0
    assert keepout == 0
    assert room == 0
    assert text == 0
    assert document["board"]["zones"][0]["net"] == "net:1"
    assert document["board"]["zones"][0]["layers"] == ["board_layer:0"]
    assert document["board"]["zones"][0]["priority"] == 4
    assert document["board"]["features"] == []
    assert document["board"]["keepouts"][0]["restrictions"] == ["copper", "via"]
    assert document["board"]["rooms"][0]["id"] == "board_room:0"
    assert document["board"]["rooms"][0]["name"] == "BGA escape"
    assert document["board"]["rooms"][0]["layers"] == ["board_layer:0"]
    assert document["board"]["rooms"][0]["priority"] == 3
    assert document["board"]["rooms"][0]["copper_clearance_mm"] == 0.075
    assert document["board"]["rooms"][0]["track_width_mm"] == 0.10
    assert document["board"]["texts"][0]["text"] == "REV A"
    assert document["board"]["texts"][0]["layer"] == "board_layer:1"
    assert document["board"]["texts"][0]["locked"] is True

    svg = board.to_svg()
    assert 'data-zone="board_zone:0"' in svg
    assert 'data-keepout="board_keepout:0"' in svg
    assert 'data-text="board_text:0"' in svg


def test_pcb_layout_composes_zones_keepouts_and_text_from_anchors():
    design = volt.Design("pcb-layout-copper-composition")
    gnd = design.net("GND", kind="ground")
    board = design.board("Control")
    front = board.add_layer("F.Cu", role="copper", side="top")
    silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(30.0, 20.0))

    with board.layout(grid=0.5) as layout:
        zone_outline = layout.rect(at=(1.24, 1.26), size=(9.01, 5.24))
        zone = layout.zone(outline=zone_outline, layers=(front,), net=gnd, priority=2)
        keepout = layout.keepout(
            layers=(front,),
            at=(12.26, 2.26),
            size=(4.24, 3.24),
            restrictions=("copper", "via"),
        )
        text = layout.text(
            "GND",
            at=board.edge("bottom").center().up(2.0),
            layer=silk,
            size=0.8,
        )

    document = json.loads(board.to_json())
    assert [anchor.point for anchor in zone_outline] == [
        (1.0, 1.5),
        (10.0, 1.5),
        (10.0, 6.5),
        (1.0, 6.5),
    ]
    assert zone == 0
    assert keepout == 0
    assert text == 0
    assert document["board"]["zones"][0]["outline"] == [
        [1.0, 1.5],
        [10.0, 1.5],
        [10.0, 6.5],
        [1.0, 6.5],
    ]
    assert document["board"]["zones"][0]["net"] == "net:0"
    assert document["board"]["zones"][0]["priority"] == 2
    assert document["board"]["keepouts"][0]["outline"] == [
        [12.5, 2.5],
        [16.5, 2.5],
        [16.5, 5.5],
        [12.5, 5.5],
    ]
    assert document["board"]["texts"][0]["position"] == [15.0, 18.0]


def test_python_board_authoring_adds_generic_board_primitives():
    design = volt.Design("generic-board-primitives")
    board = design.board("Primitives")
    front = board.add_layer("F.Cu", role="copper", side="top")
    silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(40.0, 24.0))

    board.add(volt.Hole(center=(4.0, 4.0), diameter=3.2, role="mounting", label="MH1"))
    board.add(volt.Hole(center=(36.0, 4.0), diameter=1.0, label="DRILL1", role="fixture"))
    board.add(volt.Slot(start=(8.0, 4.0), end=(16.0, 4.0), width=1.5, role="mounting"))
    board.add(
        volt.Cutout.polygon(
            ((20.0, 4.0), (25.0, 4.0), (25.0, 9.0), (20.0, 9.0)),
            role="access",
            label="CUT1",
        )
    )
    board.add(volt.Circle(center=(34.0, 4.0), diameter=1.0, role="fiducial", label="FID1"))
    board.add(volt.Hole(center=(4.0, 20.0), diameter=2.0, role="tooling", label="TH1"))
    board.add(volt.Text("REV A", at=(20.0, 20.0), layer=silk, size=1.2))
    board.add(
        volt.MechanicalKeepout(
            outline=((28.0, 14.0), (36.0, 14.0), (36.0, 20.0), (28.0, 20.0)),
            layers=(front,),
            restrictions=("copper", "via"),
        )
    )

    first_json = board.to_json()
    assert board.to_json() == first_json
    document = json.loads(first_json)
    features = document["board"]["features"]
    geometry = document["board"]["geometry"]

    assert [feature["kind"] for feature in features] == [
        "hole",
        "hole",
        "slot",
        "cutout",
        "circle",
        "hole",
    ]
    assert features[0]["role"] == "mounting"
    assert features[0]["plated"] is False
    assert features[1]["role"] == "fixture"
    assert features[2]["width_mm"] == 1.5
    assert features[3]["outline"][2] == [25.0, 9.0]
    assert features[4]["side"] == "top"
    assert features[4]["role"] == "fiducial"
    assert features[5]["role"] == "tooling"
    assert [opening["kind"] for opening in geometry["openings"]] == [
        "hole",
        "hole",
        "slot",
        "hole",
    ]
    assert geometry["cutouts"][0]["kind"] == "cutout"
    assert geometry["surface_features"][0]["kind"] == "circle"
    assert document["board"]["texts"][0]["text"] == "REV A"
    assert document["board"]["keepouts"][0]["restrictions"] == ["copper", "via"]
    assert {diagnostic.code for diagnostic in board.validate()} == set()

    svg = board.to_svg()
    assert 'class="board-feature hole"' in svg
    assert 'class="board-feature slot"' in svg
    assert 'class="board-feature cutout"' in svg
    assert 'class="board-feature circle top"' in svg
    assert 'data-keepout="board_keepout:0"' in svg
    assert 'data-text="board_text:0"' in svg

    export = board.to_kicad_pcb()
    assert [warning.construct for warning in export.warnings] == [
        "board.keepout",
        "board.feature.slot",
        "board.feature.cutout",
        "board.feature.circle",
    ]


def test_python_board_authoring_sets_rules_and_reports_drc_diagnostics():
    design, r1, _d1 = _small_resistor_led_design()
    vcc = next(net for net in design.nets() if net.name == "VCC")
    board = design.board()
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(30.0, 20.0))
    board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    board.place(r1, at=(10.0, 10.0))
    board.set_design_rules(
        copper_clearance=0.20,
        min_track_width=0.25,
        min_via_drill=0.30,
        min_via_annular=0.70,
        board_outline_clearance=0.10,
    )
    board.add_track(vcc, layer=front, points=((2.0, 2.0), (8.0, 2.0)), width=0.10)
    board.add_via(
        vcc,
        at=(12.0, 10.0),
        start_layer=front,
        end_layer=back,
        drill=0.20,
        annular=0.50,
    )

    document = json.loads(board.to_json())
    assert document["board"]["rules"] == {
        "board_outline_clearance_mm": 0.10,
        "copper_clearance_mm": 0.20,
        "minimum_track_width_mm": 0.25,
        "minimum_via_annular_diameter_mm": 0.70,
        "minimum_via_drill_diameter_mm": 0.30,
    }

    report = board.validate()
    codes = {diagnostic.code for diagnostic in report}
    assert "PCB_TRACK_WIDTH_BELOW_MINIMUM" in codes
    assert "PCB_VIA_DRILL_BELOW_MINIMUM" in codes
    assert "PCB_VIA_ANNULAR_BELOW_MINIMUM" in codes
    diagnostics_by_code = {diagnostic.code: diagnostic for diagnostic in report}
    assert diagnostics_by_code["PCB_TRACK_WIDTH_BELOW_MINIMUM"].measurement == (
        volt.DiagnosticMeasurement(actual_mm=0.10, required_mm=0.25)
    )
    assert diagnostics_by_code["PCB_VIA_DRILL_BELOW_MINIMUM"].measurement == (
        volt.DiagnosticMeasurement(actual_mm=0.20, required_mm=0.30)
    )
    assert diagnostics_by_code["PCB_VIA_ANNULAR_BELOW_MINIMUM"].measurement == (
        volt.DiagnosticMeasurement(actual_mm=0.50, required_mm=0.70)
    )
    assert any(
        entity.kind == "board_track"
        for diagnostic in report
        for entity in diagnostic.entities
    )
    assert any(
        entity.kind == "board_via"
        for diagnostic in report
        for entity in diagnostic.entities
    )


def test_python_board_authoring_sets_capability_profile_from_file_and_inline():
    design = volt.Design("capability-profile")
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(30.0, 20.0))

    profile = volt.CapabilityProfile.from_file(
        _fixture_path("example_fab_2layer.voltcap.json")
    )
    assert profile.name == "Example Fab 2-layer capability snapshot"

    board.set_capability_profile(profile)
    document = json.loads(board.to_json())
    assert document["board"]["capability_profile"]["name"] == (
        "Example Fab 2-layer capability snapshot"
    )
    assert document["board"]["capability_profile"]["provenance"] == {
        "source": "Example fixture derived from a public fabrication capability table for tests only",
        "as_of": "2026-06-11",
    }
    assert document["board"]["capability_profile"]["minimum_track_width_mm"] == 0.2
    assert document["board"]["capability_profile"]["minimum_clearances"][0] == {
        "first": "track",
        "second": "track",
        "clearance_mm": 0.2,
    }

    inline = volt.CapabilityProfile(
        name="Inline capability",
        source="Inline project data",
        as_of="2026-06-11",
        minimum_track_width=0.2,
        minimum_via_drill=0.3,
        minimum_via_annular=0.6,
        minimum_clearances=(("track", "track", 0.2),),
    )
    board.set_capability_profile(inline)
    assert json.loads(board.to_json())["board"]["capability_profile"]["name"] == (
        "Inline capability"
    )


def test_python_capability_profile_invalid_values_raise_value_error():
    design = volt.Design("invalid-capability-profile")
    board = design.board("Control")

    with pytest.raises(ValueError):
        board.set_capability_profile(
            volt.CapabilityProfile(
                name="Invalid capability",
                source="Inline project data",
                as_of="2026-06-11",
                minimum_track_width=0.0,
                minimum_via_drill=0.3,
                minimum_via_annular=0.6,
                minimum_clearances=(("track", "track", 0.2),),
            )
        )


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
    footprint_id = board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric")))
    assert board.cache_footprint(_passive_0603(("passives", "R_0603_1608Metric"))) == footprint_id
    with pytest.raises(RuntimeError, match="conflicts"):
        board.cache_footprint(
            _passive_0603(("passives", "R_0603_1608Metric"), pad_span=1.7)
        )
    with pytest.raises(IndexError, match="Volt entity id is out of range"):
        board.place(99, at=(1.0, 1.0))
    with pytest.raises(TypeError, match="Board component IDs must be integers"):
        board.place(True, at=(1.0, 1.0))
    with pytest.raises(RuntimeError, match="Component already has a board placement"):
        board.place(r1, at=(1.0, 1.0))
        board.place(r1, at=(2.0, 2.0))
    with pytest.raises(IndexError, match="Volt entity id is out of range"):
        board.add_track(99, layer=front, points=((1.0, 1.0), (2.0, 1.0)), width=0.25)
    with pytest.raises(ValueError, match="Board track width must be positive"):
        board.add_track(design.net("ROUTE"), layer=front, points=((1.0, 1.0), (2.0, 1.0)), width=0)
    with pytest.raises(ValueError, match="Board via layer span must reference distinct layers"):
        board.add_via(design.net("VIA"), at=(1.0, 1.0), start_layer=front, end_layer=front)
    with pytest.raises(ValueError, match="Board hole drill diameter must be positive"):
        board.add(volt.Hole(center=(1.0, 1.0), diameter=-1.0))
    with pytest.raises(ValueError, match="Board slot endpoints must be distinct"):
        board.add(volt.Slot(start=(1.0, 1.0), end=(1.0, 1.0), width=1.0))
    with pytest.raises(ValueError, match="Board polygon must contain at least three vertices"):
        board.add(volt.Cutout.polygon(()))
    with pytest.raises(IndexError, match="Volt entity id is out of range"):
        board.add_zone(
            outline=((1.0, 1.0), (3.0, 1.0), (3.0, 3.0), (1.0, 3.0)),
            layers=(front,),
            net=99,
        )
    with pytest.raises(RuntimeError, match="Board copper zones require copper layers"):
        silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
        board.add_zone(
            outline=((1.0, 1.0), (3.0, 1.0), (3.0, 3.0), (1.0, 3.0)),
            layers=(silk,),
            net=design.net("ZONE"),
        )
    with pytest.raises(ValueError, match="Board room copper clearance must not be negative"):
        board.add_room(
            "BGA escape",
            outline=((1.0, 1.0), (3.0, 1.0), (3.0, 3.0), (1.0, 3.0)),
            layers=(front,),
            clearance=-0.1,
        )
    with pytest.raises(ValueError, match="Board room track width must be positive"):
        board.add_room(
            "BGA escape",
            outline=((1.0, 1.0), (3.0, 1.0), (3.0, 3.0), (1.0, 3.0)),
            layers=(front,),
            track_width=0.0,
        )
    with pytest.raises(ValueError, match="Board keepout restrictions must not be empty"):
        board.add_keepout(
            outline=((1.0, 1.0), (3.0, 1.0), (3.0, 3.0), (1.0, 3.0)),
            layers=(front,),
            restrictions=(),
        )
    with pytest.raises(ValueError, match="Board text size must be positive"):
        board.add(volt.Text("REV A", at=(1.0, 1.0), layer=front, size=0.0))


def test_python_board_auto_registers_object_owned_library_footprint():
    footprint = _passive_0603(("volt.test", "Object0603"))
    library = volt.Library("volt.test")
    resistor = library.component(
        "ObjectResistor",
        pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)],
        physical_part=volt.PhysicalPartSpec(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=footprint,
            pin_pads={1: "1", 2: "2"},
        ),
        prefix="R",
    )
    design = volt.Design("object-owned-footprint")
    r1 = design.instantiate(resistor, ref="R1")
    left = design.net("LEFT")
    right = design.net("RIGHT")
    left += r1[1]
    right += r1[2]
    board = design.board("Control")
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))

    board.place(r1, at=(10.0, 6.0))
    resolutions = board.resolve_pads()

    assert [resolution.status for resolution in resolutions] == ["connected", "connected"]
    assert [resolution.pad_label for resolution in resolutions] == ["1", "2"]
    assert "PCB_FOOTPRINT_UNRESOLVED" not in {diagnostic.code for diagnostic in board.validate()}

    document = json.loads(board.to_json())
    definitions = document["board"]["footprint_definitions"]
    assert [definition["ref"] for definition in definitions] == [
        {"library": "volt.test", "name": "Object0603"}
    ]
    assert document["board"]["placements"][0]["footprint"] == "footprint_def:0"
    assert len(document["viewer"]["pad_resolutions"]) == 2

    svg = board.to_svg()
    assert 'data-footprint="volt.test:Object0603"' in svg
    assert 'class="footprint-pad' in svg


def test_python_board_ref_only_missing_geometry_still_reports_unresolved():
    design = volt.Design("missing-footprint")
    r1 = design.R("1k", ref="R1")
    left = design.net("LEFT")
    right = design.net("RIGHT")
    left += r1[1]
    right += r1[2]
    r1.select_part(
        manufacturer="Yageo",
        part_number="RC0603FR-071KL",
        package="0603",
        footprint=("missing", "NotARealFootprint"),
        pin_pads={1: "1", 2: "2"},
    )
    board = design.board()
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
    board.place(r1, at=(10.0, 6.0))

    assert board.resolve_pads() == ()
    assert "PCB_FOOTPRINT_UNRESOLVED" in {diagnostic.code for diagnostic in board.validate()}


def test_python_board_object_owned_footprints_keep_pad_mapping_diagnostics():
    footprint = _passive_0603(("volt.test", "Mapped0603"))
    missing_pad_footprint = volt.Footprint(
        ("volt.test", "Mapped0603WithExtraPad"),
        pads=(
            *footprint.pads,
            volt.FootprintPad.surface_mount("3", at=(1.6, 0.0), size=(0.8, 0.95)),
        ),
    )
    library = volt.Library("volt.test")
    unknown_pad = library.component(
        "UnknownPad",
        pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)],
        physical_part=volt.PhysicalPartSpec(
            manufacturer="Yageo",
            part_number="BADPAD",
            package="0603",
            footprint=footprint,
            pin_pads={1: "1", 2: "9"},
        ),
        prefix="R",
    )
    missing_pin = library.component(
        "MissingPin",
        pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)],
        physical_part=volt.PhysicalPartSpec(
            manufacturer="Yageo",
            part_number="MISSINGPIN",
            package="0603",
            footprint=missing_pad_footprint,
            pin_pads={1: "1", 2: "2"},
        ),
        prefix="R",
    )
    design = volt.Design("object-footprint-mapping-diagnostics")
    r1 = design.instantiate(unknown_pad, ref="R1")
    r2 = design.instantiate(missing_pin, ref="R2")
    for component in (r1, r2):
        a_net = design.net(f"{component.reference}_A")
        b_net = design.net(f"{component.reference}_B")
        a_net += component[1]
        b_net += component[2]
    board = design.board()
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(30.0, 12.0))
    board.place(r1, at=(10.0, 6.0))
    board.place(r2, at=(20.0, 6.0))

    codes = {diagnostic.code for diagnostic in board.validate()}

    assert "PCB_FOOTPRINT_UNRESOLVED" not in codes
    assert "PCB_PAD_MAPPING_UNKNOWN_PAD" in codes
    assert "PCB_PAD_MAPPING_MISSING_PIN" in codes


def test_python_board_object_owned_footprint_supports_tied_and_mechanical_pads():
    footprint = volt.Footprint(
        ("volt.test", "TieAndMechanical"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-1.0, 0.0), size=(0.6, 0.6)),
            volt.FootprintPad.surface_mount("2", at=(0.0, 0.0), size=(0.6, 0.6)),
            volt.FootprintPad.surface_mount("4", at=(1.0, 0.0), size=(0.6, 0.6)),
            volt.FootprintPad.through_hole(
                "MH",
                at=(0.0, 2.0),
                size=(1.8, 1.8),
                drill=volt.FootprintDrill(1.0, plating="non_plated"),
                layers="mechanical_hole",
                mechanical_role="mounting",
            ),
        ),
    )
    library = volt.Library("volt.test")
    connector = library.component(
        "TieAndMechanical",
        pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)],
        physical_part=volt.PhysicalPartSpec(
            manufacturer="Volt",
            part_number="TIE-MECH",
            package="custom",
            footprint=footprint,
            pin_pads={1: "1", 2: ("2", "4")},
        ),
        prefix="J",
    )
    design = volt.Design("object-footprint-tied-mechanical")
    j1 = design.instantiate(connector, ref="J1")
    a_net = design.net("A")
    a_net += j1[1]
    tied_net = design.net("B")
    tied_net += j1[2]
    board = design.board()
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
    board.place(j1, at=(10.0, 6.0))

    resolutions = {resolution.pad_label: resolution for resolution in board.resolve_pads()}

    assert resolutions["1"].status == "connected"
    assert resolutions["2"].status == "connected"
    assert resolutions["4"].status == "connected"
    assert resolutions["2"].pin == j1[2].index
    assert resolutions["4"].pin == j1[2].index
    assert resolutions["2"].net == tied_net.index
    assert resolutions["4"].net == tied_net.index
    assert resolutions["MH"].status == "non_electrical"
    assert resolutions["MH"].pin is None
    assert resolutions["MH"].net is None


def test_python_board_dedupes_object_owned_footprints_and_rejects_conflicts():
    first_footprint = _passive_0603(("volt.test", "Shared0603"))
    duplicate_footprint = _passive_0603(("volt.test", "Shared0603"))
    conflicting_footprint = _passive_0603(("volt.test", "Shared0603"), pad_span=1.9)
    library = volt.Library("volt.test")
    first = library.component(
        "First",
        pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)],
        physical_part=volt.PhysicalPartSpec.same_numbered(
            manufacturer="Yageo",
            part_number="FIRST",
            package="0603",
            footprint=first_footprint,
        ),
        prefix="R",
    )
    duplicate = library.component(
        "Duplicate",
        pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)],
        physical_part=volt.PhysicalPartSpec.same_numbered(
            manufacturer="Yageo",
            part_number="DUPLICATE",
            package="0603",
            footprint=duplicate_footprint,
        ),
        prefix="R",
    )
    conflicting = library.component(
        "Conflicting",
        pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)],
        physical_part=volt.PhysicalPartSpec.same_numbered(
            manufacturer="Yageo",
            part_number="CONFLICT",
            package="0603",
            footprint=conflicting_footprint,
        ),
        prefix="R",
    )
    design = volt.Design("object-footprint-dedupe")
    r1 = design.instantiate(first, ref="R1")
    r2 = design.instantiate(duplicate, ref="R2")
    for component in (r1, r2):
        a_net = design.net(f"{component.reference}_A")
        b_net = design.net(f"{component.reference}_B")
        a_net += component[1]
        b_net += component[2]
    board = design.board()
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(30.0, 12.0))
    board.place(r1, at=(10.0, 6.0))
    board.place(r2, at=(20.0, 6.0))

    document = json.loads(board.to_json())

    assert len(document["board"]["footprint_definitions"]) == 1
    assert [placement["footprint"] for placement in document["board"]["placements"]] == [
        "footprint_def:0",
        "footprint_def:0",
    ]
    with pytest.raises(ValueError, match="conflicts with already registered geometry"):
        design.instantiate(conflicting, ref="R3")


def test_python_board_rejects_object_owned_footprint_conflicting_with_builtin():
    footprint = _passive_0603(("passives", "R_0603_1608Metric"), pad_span=1.9)
    library = volt.Library("volt.test")
    resistor = library.component(
        "BuiltinConflict",
        pins=[volt.PinSpec("A", 1), volt.PinSpec("B", 2)],
        physical_part=volt.PhysicalPartSpec.same_numbered(
            manufacturer="Yageo",
            part_number="CONFLICT",
            package="0603",
            footprint=footprint,
        ),
        prefix="R",
    )
    design = volt.Design("object-footprint-builtin-conflict")
    r1 = design.instantiate(resistor, ref="R1")
    left = design.net("LEFT")
    right = design.net("RIGHT")
    left += r1[1]
    right += r1[2]
    board = design.board()
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(20.0, 12.0))
    board.place(r1, at=(10.0, 6.0))

    for export_or_resolution in (board.resolve_pads, board.validate, board.to_json, board.to_svg):
        with pytest.raises(RuntimeError, match="conflicts with footprint library definition"):
            export_or_resolution()


def test_python_board_stackup_authors_copper_weight_and_dielectrics():
    design, _r1, _d1 = _small_resistor_led_design()
    board = design.board("Stackup")

    front = board.add_layer(
        "F.Cu", role="copper", side="top", thickness=0.035, copper_weight=1.0
    )
    back = board.add_layer(
        "B.Cu", role="copper", side="bottom", thickness=0.035, copper_weight=2.0
    )
    board.set_layer_stack(
        (front, back), thickness=1.6, dielectrics=[(1.51, 4.6)]
    )
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))

    document = json.loads(board.to_json())

    assert document["board"]["layers"][0]["copper_weight_oz"] == 1.0
    assert document["board"]["layers"][1]["copper_weight_oz"] == 2.0
    assert document["board"]["layer_stack"]["dielectrics"] == [
        {"thickness_mm": 1.51, "relative_permittivity": 4.6}
    ]

    with pytest.raises(ValueError):
        board.add_layer("F.SilkS", role="silkscreen", side="top", copper_weight=1.0)
