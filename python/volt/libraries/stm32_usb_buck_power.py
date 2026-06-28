"""Power-stage component variants for the STM32 USB buck benchmark."""

from __future__ import annotations

from volt import Footprint, PhysicalPartSpec, SchematicSymbolSpec

from .stm32_usb_buck import (
    CAPACITOR,
    CAPACITOR_0603_FOOTPRINT,
    DIODE_SOD_123_FOOTPRINT,
    LIB,
    RESISTOR,
    RESISTOR_0603_FOOTPRINT,
    SOT_223_3_FOOTPRINT,
    SOT_23_6_FOOTPRINT,
    _digital_input,
    _ground,
    _passive,
    _power_input,
    _power_output,
    _symbol_name,
    _three_pin_regulator_symbol,
    _two_pin_symbol,
    _two_terminal_smd_footprint,
    _vertical_two_pin_symbol,
)


POLYFUSE_1206_FOOTPRINT = _two_terminal_smd_footprint(
    ("protection", "Fuse_1206_3216Metric"),
    pad_span=2.70,
    pad_width=1.20,
    pad_height=1.60,
    body_size=(3.20, 1.60),
    courtyard_size=(4.00, 2.40),
)
CAPACITOR_1206_FOOTPRINT = _two_terminal_smd_footprint(
    ("passives", "C_1206_3216Metric"),
    pad_span=2.70,
    pad_width=1.20,
    pad_height=1.60,
    body_size=(3.20, 1.60),
    courtyard_size=(4.00, 2.40),
)
POWER_INDUCTOR_4X4_FOOTPRINT = _two_terminal_smd_footprint(
    ("inductors", "L_4.0x4.0mm"),
    pad_span=3.40,
    pad_width=1.40,
    pad_height=2.40,
    body_size=(4.00, 4.00),
    courtyard_size=(4.80, 4.80),
)


def _buck_regulator_symbol(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        _symbol_name(name),
        pins=(
            SchematicSymbolSpec.pin("IN", 5, (0, 12), "Left"),
            SchematicSymbolSpec.pin("EN", 4, (0, 28), "Left"),
            SchematicSymbolSpec.pin("FB", 3, (0, 36), "Left"),
            SchematicSymbolSpec.pin("SW", 6, (64, 12), "Right"),
            SchematicSymbolSpec.pin("BST", 1, (64, -8), "Right"),
            SchematicSymbolSpec.pin("GND", 2, (32, 48), "Down"),
        ),
        primitives=(
            SchematicSymbolSpec.rectangle((12, 0), (52, 40)),
            SchematicSymbolSpec.line((0, 12), (12, 12)),
            SchematicSymbolSpec.line((0, 28), (12, 28)),
            SchematicSymbolSpec.line((0, 36), (12, 36)),
            SchematicSymbolSpec.line((52, 12), (64, 12)),
            SchematicSymbolSpec.line((52, -8), (64, -8)),
            SchematicSymbolSpec.line((32, 40), (32, 48)),
            SchematicSymbolSpec.text("BUCK", (32, 18)),
        ),
    )


def _two_pin_library_component(
    name: str,
    *,
    manufacturer: str,
    part_number: str,
    package: str,
    footprint: Footprint,
    prefix: str,
    symbol: SchematicSymbolSpec | None = None,
    properties: dict[str, str] | None = None,
    voltage_rating: float | None = None,
    power_rating: float | None = None,
):
    return LIB.component(
        name,
        pins=[_passive("1", 1), _passive("2", 2)],
        properties={"category": "passive"} if properties is None else properties,
        physical_part=PhysicalPartSpec.same_numbered(
            manufacturer=manufacturer,
            part_number=part_number,
            package=package,
            footprint=footprint,
            voltage_rating=voltage_rating,
            power_rating=power_rating,
        ),
        prefix=prefix,
        schematic_symbol=symbol,
    )


def _resistor_value(name: str, part_number: str) -> object:
    return _two_pin_library_component(
        name,
        manufacturer="Yageo",
        part_number=part_number,
        package="0603",
        footprint=RESISTOR_0603_FOOTPRINT,
        prefix="R",
        symbol=_two_pin_symbol(name, "R"),
        power_rating=0.1,
    )


def _capacitor_value(
    name: str,
    part_number: str,
    *,
    package: str,
    footprint: Footprint,
    voltage_rating: float,
    manufacturer: str = "Murata",
) -> object:
    return _two_pin_library_component(
        name,
        manufacturer=manufacturer,
        part_number=part_number,
        package=package,
        footprint=footprint,
        prefix="C",
        symbol=_vertical_two_pin_symbol(name, "C"),
        voltage_rating=voltage_rating,
    )


BUCK_MP2359 = LIB.component(
    "MP2359_Buck",
    pins=[
        _passive("BST", 1),
        _ground("GND", 2),
        _passive("FB", 3),
        _digital_input("EN", 4),
        _passive("IN", 5),
        _power_output("SW", 6),
    ],
    properties={"category": "regulator", "topology": "buck"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Monolithic Power Systems",
        part_number="MP2359DJ-LF-Z",
        package="SOT-23-6",
        footprint=SOT_23_6_FOOTPRINT,
    ),
    prefix="U",
    schematic_symbol=_buck_regulator_symbol("MP2359_Buck"),
)

AP1117_33 = LIB.component(
    "AP1117-33",
    pins=[
        _ground("GND", 1),
        _power_output("VO", 2),
        _power_input("VI", 3),
    ],
    properties={"category": "regulator"},
    physical_part=PhysicalPartSpec(
        manufacturer="Diodes Incorporated",
        part_number="AP1117E33G-13",
        package="SOT-223-3",
        footprint=SOT_223_3_FOOTPRINT,
        pin_pads={1: "1", 2: ("2", "4"), 3: "3"},
    ),
    prefix="U",
    schematic_symbol=_three_pin_regulator_symbol("AP1117_33"),
)

POLYFUSE = LIB.component(
    "Polyfuse",
    pins=[_passive("1", 1), _passive("2", 2)],
    properties={"category": "protection"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Bourns",
        part_number="MF-NSMF020-2",
        package="1206 resettable fuse",
        footprint=POLYFUSE_1206_FOOTPRINT,
    ),
    prefix="F",
    schematic_symbol=_two_pin_symbol("Polyfuse", "F"),
)

SCHOTTKY_B5819 = LIB.component(
    "B5819W",
    pins=[_passive("A", 1), _passive("K", 2)],
    properties={"category": "diode", "topology_role": "buck_catch"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Diodes Incorporated",
        part_number="B5819W-7-F",
        package="SOD-123",
        footprint=DIODE_SOD_123_FOOTPRINT,
    ),
    prefix="D",
    schematic_symbol=_two_pin_symbol("B5819W", "D"),
)

RES_10K = RESISTOR
RES_330R = _resistor_value("Resistor_330R", "RC0603FR-07330RL")
RES_15K = _resistor_value("Resistor_15k", "RC0603FR-0715KL")
RES_47K = _resistor_value("Resistor_47k", "RC0603FR-0747KL")
RES_68K = _resistor_value("Resistor_68k", "RC0603FR-0768KL")
RES_100K = _resistor_value("Resistor_100k", "RC0603FR-07100KL")

CAP_100NF = CAPACITOR
CAP_10NF = _capacitor_value(
    "Capacitor_10nF",
    "GRM188R71H103KA01D",
    package="0603",
    footprint=CAPACITOR_0603_FOOTPRINT,
    voltage_rating=50,
)
CAP_18PF = _capacitor_value(
    "Capacitor_18pF",
    "GRM1885C1H180JA01D",
    package="0603",
    footprint=CAPACITOR_0603_FOOTPRINT,
    voltage_rating=50,
)
CAP_2U2 = _capacitor_value(
    "Capacitor_2u2",
    "GRM188R61A225KE34D",
    package="0603",
    footprint=CAPACITOR_0603_FOOTPRINT,
    voltage_rating=10,
)
CAP_4U7 = _capacitor_value(
    "Capacitor_4u7",
    "GRM188R60J475ME19D",
    package="0603",
    footprint=CAPACITOR_0603_FOOTPRINT,
    voltage_rating=6.3,
)
CAP_10UF = _capacitor_value(
    "Capacitor_10uF",
    "CL31A106KAHNNNE",
    package="1206",
    footprint=CAPACITOR_1206_FOOTPRINT,
    voltage_rating=25,
    manufacturer="Samsung Electro-Mechanics",
)

POWER_INDUCTOR_10UH = _two_pin_library_component(
    "Inductor_10uH",
    manufacturer="Bourns",
    part_number="SRN4018-100M",
    package="4.0x4.0mm shielded inductor",
    footprint=POWER_INDUCTOR_4X4_FOOTPRINT,
    prefix="L",
    symbol=_two_pin_symbol("Inductor_10uH", "L"),
)


__all__ = [
    "AP1117_33",
    "BUCK_MP2359",
    "CAP_10NF",
    "CAP_10UF",
    "CAP_100NF",
    "CAP_18PF",
    "CAP_2U2",
    "CAP_4U7",
    "POLYFUSE",
    "POWER_INDUCTOR_10UH",
    "RES_10K",
    "RES_15K",
    "RES_330R",
    "RES_47K",
    "RES_68K",
    "RES_100K",
    "SCHOTTKY_B5819",
]
