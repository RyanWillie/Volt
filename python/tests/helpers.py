import volt


def _two_pin_test_symbol(name: str, *, variant: str = "default", label: str = "SYM"):
    return volt.SchematicSymbolSpec(
        name,
        variant=variant,
        pins=(
            volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),
            volt.SchematicSymbolSpec.pin("2", 2, (20, 0), "Right"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.line((0, 0), (4, 0)),
            volt.SchematicSymbolSpec.rectangle((4, -3), (16, 3)),
            volt.SchematicSymbolSpec.line((16, 0), (20, 0)),
            volt.SchematicSymbolSpec.text(label, (10, -8)),
        ),
    )


def _definition_for_component(circuit: dict, reference: str) -> dict:
    component = next(item for item in circuit["components"] if item["reference"] == reference)
    definition_index = int(component["definition"].split(":")[1])
    return circuit["component_definitions"][definition_index]


def _common_catalog_components(design: volt.Design):
    return [
        ("R1", design.R("10k", ref="R1"), "volt.passives:resistor", ("1", "2")),
        ("C1", design.C("100nF", ref="C1"), "volt.passives:capacitor", ("1", "2")),
        ("C2", design.CP("10uF", ref="C2"), "volt.passives:capacitor_polarized", ("1", "2")),
        ("L1", design.L("10uH", ref="L1"), "volt.passives:inductor", ("1", "2")),
        ("D1", design.diode(ref="D1"), "volt.discretes:diode", ("1", "2")),
        ("D2", design.LED(ref="D2"), "volt.optos:led", ("1", "2")),
        ("SW1", design.switch(ref="SW1"), "volt.switches:switch_spst", ("1", "2")),
        ("Y1", design.crystal(ref="Y1"), "volt.frequency:crystal_2pin", ("1", "2")),
        ("TP1", design.test_point(ref="TP1"), "volt.testpoints:test_point", ("1",)),
        ("J1", design.connector_1x01(ref="J1"), "volt.connectors:connector_1x01", ("1",)),
        ("J2", design.connector_1x02(ref="J2"), "volt.connectors:connector_1x02", ("1", "2")),
        (
            "J3",
            design.connector_1x03(ref="J3"),
            "volt.connectors:connector_1x03",
            ("1", "2", "3"),
        ),
        ("U1", design.regulator(ref="U1"), "volt.power:regulator_3pin", ("3", "2", "1")),
        (
            "U2",
            design.op_amp(ref="U2"),
            "volt.analog:op_amp_5pin",
            ("3", "2", "1", "5", "4"),
        ),
    ]


def _wire_points(projection, index=0):
    return [
        (point["x"], point["y"])
        for point in projection["wire_runs"][index]["points"]
    ]
