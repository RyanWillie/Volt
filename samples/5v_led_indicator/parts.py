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
