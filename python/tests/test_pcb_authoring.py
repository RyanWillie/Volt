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


def test_python_board_authoring_writes_deterministic_json_and_svg(tmp_path):
    design, r1, d1 = _small_resistor_led_design()
    led_a = next(net for net in design.nets() if net.name == "LED_A")
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
    assert document["board"]["features"][0]["kind"] == "mounting_hole"
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

    json_path = tmp_path / "board.voltpcb.json"
    svg_path = tmp_path / "board.svg"
    board.write_json(json_path)
    board.write_svg(svg_path)
    assert json_path.read_text(encoding="utf-8") == first_json
    assert svg_path.read_text(encoding="utf-8") == svg


def test_python_board_authoring_writes_zones_keepouts_and_text():
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
    keepout = board.add_keepout(
        outline=((12.0, 2.0), (16.0, 2.0), (16.0, 6.0), (12.0, 6.0)),
        layers=(front,),
        restrictions=("copper", "via"),
    )
    text = board.add_text("REV A", at=(5.0, 15.0), layer=silk, rotation=90.0, size=1.2, locked=True)

    document = json.loads(board.to_json())
    assert zone == 0
    assert keepout == 0
    assert text == 0
    assert document["board"]["zones"][0]["net"] == "net:1"
    assert document["board"]["zones"][0]["layers"] == ["board_layer:0"]
    assert document["board"]["zones"][0]["priority"] == 4
    assert document["board"]["keepouts"][0]["restrictions"] == ["copper", "via"]
    assert document["board"]["texts"][0]["text"] == "REV A"
    assert document["board"]["texts"][0]["layer"] == "board_layer:1"
    assert document["board"]["texts"][0]["locked"] is True

    svg = board.to_svg()
    assert 'data-zone="board_zone:0"' in svg
    assert 'data-keepout="board_keepout:0"' in svg
    assert 'data-text="board_text:0"' in svg


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
    with pytest.raises(ValueError, match="Board keepout restrictions must not be empty"):
        board.add_keepout(
            outline=((1.0, 1.0), (3.0, 1.0), (3.0, 3.0), (1.0, 3.0)),
            layers=(front,),
            restrictions=(),
        )
    with pytest.raises(ValueError, match="Board text size must be positive"):
        board.add_text("REV A", at=(1.0, 1.0), layer=front, size=0.0)


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
