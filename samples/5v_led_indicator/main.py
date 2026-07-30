import volt


LIBRARY = volt.Library("volt.samples.5v_led_indicator", version="1.0.0")

MOLEX_DATASHEET = "https://www.molex.com/en-us/products/part-detail/22284020"
YAGEO_DATASHEET = (
    "https://www.yageo.com/upload/media/product/productsearch/datasheet/rchip/RC_L.pdf"
)
LITEON_DATASHEET = (
    "https://optoelectronics.liteon.com/upload/download/"
    "DS-22-99-0151/LTST-C190KRKT.PDF"
)


def _connector_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec.block(
        "volt.samples.5v_led_indicator:input_1x02",
        pins=(
            volt.SchematicSymbolSpec.block_pin("+5V", 1, side="right", slot=1),
            volt.SchematicSymbolSpec.block_pin("GND", 2, side="right", slot=2),
        ),
        width=22,
        height=30,
        center_label="5 V IN",
        pin_labels=True,
        pin_numbers=True,
    )


def _resistor_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.samples.5v_led_indicator:resistor",
        pins=(
            volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Right"),
            volt.SchematicSymbolSpec.pin("2", 2, (20, 0), "Left"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.terminal_lead((0, 0), (5, 0), terminal="start"),
            volt.SchematicSymbolSpec.rectangle((5, -3), (15, 3)),
            volt.SchematicSymbolSpec.terminal_lead((15, 0), (20, 0), terminal="end"),
        ),
    )


def _led_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.samples.5v_led_indicator:red_led",
        pins=(
            volt.SchematicSymbolSpec.pin("A", 2, (0, 0), "Right"),
            volt.SchematicSymbolSpec.pin("K", 1, (20, 0), "Left"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.terminal_lead((0, 0), (7, 0), terminal="start"),
            volt.SchematicSymbolSpec.line((7, -5), (13, 0)),
            volt.SchematicSymbolSpec.line((7, 5), (13, 0)),
            volt.SchematicSymbolSpec.line((13, -5), (13, 5)),
            volt.SchematicSymbolSpec.terminal_lead((13, 0), (20, 0), terminal="end"),
            volt.SchematicSymbolSpec.line((12, -7), (16, -11)),
            volt.SchematicSymbolSpec.line((15, -6), (19, -10)),
        ),
    )


INPUT_5V = LIBRARY.part(
    "MOLEX-22-28-4020",
    pins=(
        volt.PinSpec("+5V", 1, role="power_output", voltage_range=(0.0, 5.0)),
        volt.PinSpec("GND", 2, role="ground"),
    ),
    symbol=_connector_symbol(),
    footprint=volt.Footprint(
        ("volt.samples.5v_led_indicator", "Molex_KK_254_1x02_P2.54mm_Vertical"),
        pads=(
            volt.FootprintPad.through_hole(
                "1",
                at=(0.0, -1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
                shape="rounded_rectangle",
            ),
            volt.FootprintPad.through_hole(
                "2",
                at=(0.0, 1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
        ),
        courtyard=((-2.75, -3.25), (2.75, -3.25), (2.75, 3.25), (-2.75, 3.25)),
        body=((-2.5, -3.0), (2.5, -3.0), (2.5, 3.0), (-2.5, 3.0)),
        fabrication_outline=(
            (-2.5, -3.0),
            (2.5, -3.0),
            (2.5, 3.0),
            (-2.5, 3.0),
        ),
        assembly_outline=(
            (-2.5, -3.0),
            (2.5, -3.0),
            (2.5, 3.0),
            (-2.5, 3.0),
        ),
    ),
    pads={1: "1", 2: "2"},
    value="5 V INPUT",
    manufacturer="Molex",
    mpn="22-28-4020",
    package="KK 254 1x02 vertical through-hole",
    voltage_rating=250.0,
    provenance=volt.PartProvenance(
        datasheet=MOLEX_DATASHEET,
        authored_by="Volt 5 V LED indicator sample",
    ),
    prefix="J",
)

RESISTOR_330R = LIBRARY.part(
    "RC0603FR-07330RL",
    pins=(
        volt.PinSpec("1", 1, role="passive"),
        volt.PinSpec("2", 2, role="passive"),
    ),
    symbol=_resistor_symbol(),
    footprint=volt.Footprint(
        ("volt.samples.5v_led_indicator", "R_0603_1608Metric"),
        pads=(
            volt.FootprintPad.surface_mount("1", at=(-0.9, 0.0), size=(0.9, 0.95)),
            volt.FootprintPad.surface_mount("2", at=(0.9, 0.0), size=(0.9, 0.95)),
        ),
        courtyard=((-1.4, -0.8), (1.4, -0.8), (1.4, 0.8), (-1.4, 0.8)),
        body=((-0.8, -0.4), (0.8, -0.4), (0.8, 0.4), (-0.8, 0.4)),
        fabrication_outline=(
            (-0.8, -0.4),
            (0.8, -0.4),
            (0.8, 0.4),
            (-0.8, 0.4),
        ),
        assembly_outline=(
            (-0.8, -0.4),
            (0.8, -0.4),
            (0.8, 0.4),
            (-0.8, 0.4),
        ),
    ),
    pads={1: "1", 2: "2"},
    value="330 ohm",
    manufacturer="Yageo",
    mpn="RC0603FR-07330RL",
    package="0603 (1608 metric)",
    properties={"tolerance": "1%"},
    voltage_rating=75.0,
    provenance=volt.PartProvenance(
        datasheet=YAGEO_DATASHEET,
        authored_by="Volt 5 V LED indicator sample",
    ),
    prefix="R",
)

_LED_JUNCTION = volt.ElectricalSubject.directed_relation("junction")
_LED_SCHEMA = volt.FeatureSchema.diode_junction()
_LED_CONTRACT = volt.ComponentContract(
    key="volt.samples.5v_led_indicator/red-led@1",
    pin_keys=("A", "K"),
    relations=(volt.ContractDirectedRelation("junction", "A", "K"),),
    feature_schemas=(_LED_SCHEMA,),
    feature_bindings=(
        volt.FeatureBinding(
            "junction",
            _LED_SCHEMA.key,
            _LED_JUNCTION,
            (
                volt.FeatureRoleBinding("positive", ("A",)),
                volt.FeatureRoleBinding("negative", ("K",)),
            ),
        ),
    ),
)
_LED_TEST_CURRENT = volt.ElectricalCondition.equal(
    _LED_JUNCTION,
    "current",
    volt.ElectricalValueExpression.literal(0.020),
)

RED_LED = LIBRARY.part(
    "LTST-C190KRKT",
    pins=(
        volt.PinSpec("A", 2, role="passive"),
        volt.PinSpec("K", 1, role="passive"),
    ),
    symbol=_led_symbol(),
    footprint=volt.Footprint(
        ("volt.samples.5v_led_indicator", "LED_0603_1608Metric"),
        pads=(
            volt.FootprintPad.surface_mount("2", at=(-0.9, 0.0), size=(0.9, 0.95)),
            volt.FootprintPad.surface_mount("1", at=(0.9, 0.0), size=(0.9, 0.95)),
        ),
        courtyard=((-1.4, -0.8), (1.4, -0.8), (1.4, 0.8), (-1.4, 0.8)),
        body=((-0.8, -0.4), (0.8, -0.4), (0.8, 0.4), (-0.8, 0.4)),
        fabrication_outline=(
            (-0.8, -0.4),
            (0.8, -0.4),
            (0.8, 0.4),
            (-0.8, 0.4),
        ),
        assembly_outline=(
            (-0.8, -0.4),
            (0.8, -0.4),
            (0.8, 0.4),
            (-0.8, 0.4),
        ),
        markings=(
            volt.FootprintMarking.polarity(
                ((0.49, -0.3), (0.61, -0.3), (0.61, 0.3), (0.49, 0.3))
            ),
        ),
    ),
    pads={"A": "2", "K": "1"},
    value="RED",
    manufacturer="Lite-On",
    mpn="LTST-C190KRKT",
    package="0603 (1608 metric)",
    contract=_LED_CONTRACT,
    electrical_records=(
        volt.ElectricalRecord(
            _LED_JUNCTION,
            "voltage",
            "characteristic",
            "envelope",
            minimum=1.6,
            typical=2.0,
            maximum=2.4,
            conditions=(_LED_TEST_CURRENT,),
        ),
        volt.ElectricalRecord.absolute_current(_LED_JUNCTION, maximum=0.030),
        volt.ElectricalRecord.absolute_voltage(_LED_JUNCTION, minimum=-5.0),
    ),
    provenance=volt.PartProvenance(
        datasheet=LITEON_DATASHEET,
        authored_by="Volt 5 V LED indicator sample",
    ),
    prefix="D",
)


def _capability_profile() -> volt.CapabilityProfile:
    return volt.CapabilityProfile(
        name="Volt reference two-layer process",
        source="Volt 5 V LED indicator sample",
        as_of="2026-07-30",
        minimum_track_width=0.20,
        minimum_via_drill=0.30,
        minimum_via_annular=0.70,
        minimum_clearances=(("track", "track", 0.20), ("track", "pad", 0.20)),
        supported_copper_layer_counts=(2,),
        board_thickness_range=(1.5, 1.7),
        available_copper_weights=(1.0,),
        drill_diameter_range=(0.30, 6.0),
    )


def main() -> volt.Project:
    project = volt.Project(
        "5v-led-indicator",
        version="1.0.0",
        description="A complete 5 V red LED indicator reference project",
    )
    project.use_library(LIBRARY)

    @project.design
    def design():
        result = volt.Design("5v-led-indicator")
        connector = result.instantiate(
            INPUT_5V, ref="J1", properties=dict(INPUT_5V.properties)
        ).dnp(False)
        resistor = result.instantiate(
            RESISTOR_330R, ref="R1", properties=dict(RESISTOR_330R.properties)
        ).dnp(False)
        led = result.instantiate(
            RED_LED, ref="D1", properties=dict(RED_LED.properties)
        ).dnp(False)

        five_volts = result.net("+5V", kind="power", voltage=5.0)
        led_anode = result.net("LED_A")
        ground = result.net("GND", kind="ground")
        five_volts += connector["+5V"], resistor[1]
        led_anode += resistor[2], led["A"]
        ground += led["K"], connector["GND"]
        return result

    @project.schematic
    def schematic(context):
        design = context.design()
        sheet = design.schematic(
            "Main",
            size=(260, 140),
            orientation="landscape",
            title="5 V LED Indicator",
            number=1,
            page_count=1,
            revision="A",
            date="2026-07-30",
            project="Volt 5 V LED Indicator",
            file="main.py",
            margins=(10, 10, 10, 10),
            coordinate_zones=(6, 4),
            grid={"spacing": 5, "visible": False},
        )
        nets = {net.name: net for net in design.nets()}
        with sheet.drawing(unit=1) as drawing:
            connector = (
                drawing.place(design.component("J1"), at=(30, 45))
                .label_ref(loc="top", offset=5)
                .label_value(loc="bottom", offset=8)
            )
            resistor = (
                drawing.two_terminal(design.component("R1"))
                .at(connector["+5V"].right(25))
                .right(45)
                .label_ref(loc="top", offset=8)
                .label_value(loc="bottom", offset=10)
            )
            led = (
                drawing.two_terminal(design.component("D1"))
                .at(resistor.end.right(25))
                .right(45)
                .label_ref(loc="top", offset=8)
                .label_value(loc="bottom", offset=10)
            )
            drawing.connect(
                connector["+5V"], resistor.start, net=nets["+5V"], shape="-"
            )
            drawing.connect(resistor.end, led.start, net=nets["LED_A"], shape="-")
            drawing.connect(
                led.end,
                connector["GND"],
                net=nets["GND"],
                shape="|-",
                k=25,
            )
        return sheet

    @project.board
    def board(context):
        design = context.design()
        board = design.add_board("Indicator")
        board.set_capability_profile(_capability_profile())
        board.set_design_rules(
            copper_clearance=0.25,
            min_track_width=0.25,
            min_via_drill=0.35,
            min_via_annular=0.75,
            board_outline_clearance=0.25,
            package_assembly_clearance=0.25,
        )
        board.set_rectangular_outline(origin=(0.0, 0.0), size=(28.0, 14.0))
        front = board.add_layer("F.Cu", role="copper", side="top", copper_weight=1.0)
        back = board.add_layer("B.Cu", role="copper", side="bottom", copper_weight=1.0)
        silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
        board.set_layer_stack((front, back), thickness=1.6)

        nets = {net.name: net for net in design.nets()}
        with board.layout(unit=1.0, grid=0.25) as layout:
            connector = layout.place(
                design.component("J1"),
                at=layout.snap((4.0, 7.0)),
                orient="right",
                locked=True,
            )
            resistor = (
                layout.two_pad(design.component("R1"))
                .at(layout.snap(connector["+5V"].right(7.0)))
                .anchor("start")
                .right()
            )
            led = (
                layout.two_pad(design.component("D1"))
                .at(layout.snap(resistor.end.right(5.0)))
                .anchor("start")
                .right()
            )

            layout.connect(
                connector["+5V"],
                resistor.start,
                layer=front,
                net=nets["+5V"],
                width=0.35,
            )
            layout.connect(
                resistor.end,
                led.start,
                layer=front,
                net=nets["LED_A"],
                width=0.25,
            )
            ground_lane = layout.snap(led.end.down(3.0))
            layout.connect(
                led.end,
                connector["GND"],
                layer=front,
                net=nets["GND"],
                width=0.35,
                through=(
                    ground_lane,
                    ground_lane.tox(connector["GND"]),
                ),
            )

            layout.text(
                "5V LED REV A",
                at=board.edge("bottom").center().up(1.0),
                layer=silk,
                size=0.8,
            )
        return board

    @project.design.test
    def indicator_topology(check):
        check.net("+5V").connects("J1.+5V", "R1.1")
        check.net("LED_A").connects("R1.2", "D1.A")
        check.net("GND").connects("D1.K", "J1.GND")
        check.no_connection("+5V", "GND")

    @project.schematic.test
    def schematic_is_complete(check):
        check.places("J1", "R1", "D1")

    @project.board.test
    def board_is_complete(check):
        check.has_outline()
        check.places("J1", "R1", "D1")

    return project
