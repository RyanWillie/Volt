"""Native component library for the STM32 USB buck benchmark."""

from __future__ import annotations

from volt import Library, PhysicalPartSpec, PinSpec, SchematicSymbolSpec

LIB = Library("volt.benchmarks.stm32_usb_buck")


def _bidirectional(name: str, number: int | str) -> PinSpec:
    return PinSpec(
        name,
        number,
        role="bidirectional",
        terminal="signal",
        direction="bidirectional",
        signal="digital",
    )


def _digital_input(name: str, number: int | str) -> PinSpec:
    return PinSpec(
        name,
        number,
        role="input",
        terminal="signal",
        direction="input",
        signal="digital",
    )


def _power_input(name: str, number: int | str) -> PinSpec:
    return PinSpec(name, number, role="power", terminal="power", direction="input")


def _power_output(name: str, number: int | str) -> PinSpec:
    return PinSpec(name, number, role="power_output", terminal="power", direction="output")


def _ground(name: str, number: int | str) -> PinSpec:
    return PinSpec(name, number, role="ground", terminal="ground", direction="passive")


def _passive(name: str, number: int | str) -> PinSpec:
    return PinSpec(name, number, role="passive", terminal="passive", direction="passive")


def _symbol_name(name: str) -> str:
    return f"{LIB.namespace}:{name}"


def _two_pin_symbol(name: str, label: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        _symbol_name(name),
        pins=(
            SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),
            SchematicSymbolSpec.pin("2", 2, (20, 0), "Right"),
        ),
        primitives=(
            SchematicSymbolSpec.line((0, 0), (4, 0)),
            SchematicSymbolSpec.rectangle((4, -3), (16, 3)),
            SchematicSymbolSpec.line((16, 0), (20, 0)),
            SchematicSymbolSpec.text(label, (10, -8)),
        ),
    )


def _vertical_two_pin_symbol(name: str, label: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        _symbol_name(name),
        pins=(
            SchematicSymbolSpec.pin("1", 1, (0, 0), "Up"),
            SchematicSymbolSpec.pin("2", 2, (0, 20), "Down"),
        ),
        primitives=(
            SchematicSymbolSpec.line((0, 0), (0, 4)),
            SchematicSymbolSpec.rectangle((-3, 4), (3, 16)),
            SchematicSymbolSpec.line((0, 16), (0, 20)),
            SchematicSymbolSpec.text(label, (8, 10)),
        ),
    )


def _three_pin_regulator_symbol(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        _symbol_name(name),
        pins=(
            SchematicSymbolSpec.pin("VI", 3, (0, 10), "Left"),
            SchematicSymbolSpec.pin("VO", 2, (50, 10), "Right"),
            SchematicSymbolSpec.pin("GND", 1, (25, 30), "Down"),
        ),
        primitives=(
            SchematicSymbolSpec.rectangle((10, 0), (40, 25)),
            SchematicSymbolSpec.line((0, 10), (10, 10)),
            SchematicSymbolSpec.line((40, 10), (50, 10)),
            SchematicSymbolSpec.line((25, 25), (25, 30)),
            SchematicSymbolSpec.text("LDO", (25, 14)),
        ),
    )


def _connector_symbol(name: str, pins: tuple[PinSpec, ...], label: str) -> SchematicSymbolSpec:
    symbol_pins = []
    primitives = [
        SchematicSymbolSpec.rectangle((10, -5), (34, (len(pins) - 1) * 8 + 5)),
        SchematicSymbolSpec.text(label, (22, -10)),
    ]
    for index, pin in enumerate(pins):
        y = index * 8
        symbol_pins.append(SchematicSymbolSpec.pin(pin.name, pin.number, (0, y), "Left"))
        primitives.append(SchematicSymbolSpec.line((0, y), (10, y)))
    return SchematicSymbolSpec(
        _symbol_name(name),
        pins=tuple(symbol_pins),
        primitives=tuple(primitives),
    )


def _usb_protection_symbol(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        _symbol_name(name),
        pins=(
            SchematicSymbolSpec.pin("I/O1", 1, (0, 8), "Left"),
            SchematicSymbolSpec.pin("I/O2", 3, (0, 20), "Left"),
            SchematicSymbolSpec.pin("I/O4", 6, (54, 8), "Right"),
            SchematicSymbolSpec.pin("I/O3", 4, (54, 20), "Right"),
            SchematicSymbolSpec.pin("VBUS", 5, (27, -10), "Up"),
            SchematicSymbolSpec.pin("GND", 2, (27, 38), "Down"),
        ),
        primitives=(
            SchematicSymbolSpec.rectangle((10, 0), (44, 30)),
            SchematicSymbolSpec.line((0, 8), (10, 8)),
            SchematicSymbolSpec.line((0, 20), (10, 20)),
            SchematicSymbolSpec.line((44, 8), (54, 8)),
            SchematicSymbolSpec.line((44, 20), (54, 20)),
            SchematicSymbolSpec.line((27, -10), (27, 0)),
            SchematicSymbolSpec.line((27, 30), (27, 38)),
            SchematicSymbolSpec.text("ESD", (27, 16)),
        ),
    )


def _crystal_symbol(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        _symbol_name(name),
        pins=(
            SchematicSymbolSpec.pin("1", 1, (0, 10), "Left"),
            SchematicSymbolSpec.pin("3", 3, (44, 10), "Right"),
            SchematicSymbolSpec.pin("GND", 2, (14, 28), "Down"),
            SchematicSymbolSpec.pin("GND", 4, (30, 28), "Down"),
        ),
        primitives=(
            SchematicSymbolSpec.line((0, 10), (12, 10)),
            SchematicSymbolSpec.line((32, 10), (44, 10)),
            SchematicSymbolSpec.line((18, 0), (18, 20)),
            SchematicSymbolSpec.line((26, 0), (26, 20)),
            SchematicSymbolSpec.rectangle((12, 4), (32, 16)),
            SchematicSymbolSpec.line((14, 16), (14, 28)),
            SchematicSymbolSpec.line((30, 16), (30, 28)),
            SchematicSymbolSpec.text("XTAL", (22, -6)),
        ),
    )


def _switch_symbol(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        _symbol_name(name),
        pins=(
            SchematicSymbolSpec.pin("A", 1, (0, 0), "Left"),
            SchematicSymbolSpec.pin("C", 2, (44, 10), "Right"),
            SchematicSymbolSpec.pin("B", 3, (0, 20), "Left"),
        ),
        primitives=(
            SchematicSymbolSpec.line((0, 0), (14, 0)),
            SchematicSymbolSpec.line((0, 20), (14, 20)),
            SchematicSymbolSpec.line((30, 10), (44, 10)),
            SchematicSymbolSpec.line((14, 0), (30, 10)),
            SchematicSymbolSpec.text("SW", (22, -8)),
        ),
    )


def _stm32_symbol(name: str, pins: tuple[PinSpec, ...]) -> SchematicSymbolSpec:
    symbol_pins = []
    primitives = [
        SchematicSymbolSpec.rectangle((20, -10), (80, 165)),
        SchematicSymbolSpec.text("STM32F405", (50, 70)),
    ]
    for index, pin in enumerate(pins):
        if index < 32:
            y = index * 5
            symbol_pins.append(SchematicSymbolSpec.pin(pin.name, pin.number, (0, y), "Left"))
            primitives.append(SchematicSymbolSpec.line((0, y), (20, y)))
        else:
            y = (63 - index) * 5
            symbol_pins.append(SchematicSymbolSpec.pin(pin.name, pin.number, (100, y), "Right"))
            primitives.append(SchematicSymbolSpec.line((80, y), (100, y)))
    return SchematicSymbolSpec(
        _symbol_name(name),
        pins=tuple(symbol_pins),
        primitives=tuple(primitives),
    )


STM32F405RGTx_PINS = (
    _power_input("VBAT", 1),
    _bidirectional("PC13", 2),
    _bidirectional("PC14", 3),
    _bidirectional("PC15", 4),
    _digital_input("PH0", 5),
    _digital_input("PH1", 6),
    _digital_input("NRST", 7),
    _bidirectional("PC0", 8),
    _bidirectional("PC1", 9),
    _bidirectional("PC2", 10),
    _bidirectional("PC3", 11),
    _ground("VSSA", 12),
    _power_input("VDDA", 13),
    _bidirectional("PA0", 14),
    _bidirectional("PA1", 15),
    _bidirectional("PA2", 16),
    _bidirectional("PA3", 17),
    _ground("VSS", 18),
    _power_input("VDD", 19),
    _bidirectional("PA4", 20),
    _bidirectional("PA5", 21),
    _bidirectional("PA6", 22),
    _bidirectional("PA7", 23),
    _bidirectional("PC4", 24),
    _bidirectional("PC5", 25),
    _bidirectional("PB0", 26),
    _bidirectional("PB1", 27),
    _bidirectional("PB2", 28),
    _bidirectional("PB10", 29),
    _bidirectional("PB11", 30),
    _power_output("VCAP_1", 31),
    _power_input("VDD", 32),
    _bidirectional("PB12", 33),
    _bidirectional("PB13", 34),
    _bidirectional("PB14", 35),
    _bidirectional("PB15", 36),
    _bidirectional("PC6", 37),
    _bidirectional("PC7", 38),
    _bidirectional("PC8", 39),
    _bidirectional("PC9", 40),
    _bidirectional("PA8", 41),
    _bidirectional("PA9", 42),
    _bidirectional("PA10", 43),
    _bidirectional("PA11", 44),
    _bidirectional("PA12", 45),
    _bidirectional("PA13", 46),
    _power_output("VCAP_2", 47),
    _power_input("VDD", 48),
    _bidirectional("PA14", 49),
    _bidirectional("PA15", 50),
    _bidirectional("PC10", 51),
    _bidirectional("PC11", 52),
    _bidirectional("PC12", 53),
    _bidirectional("PD2", 54),
    _bidirectional("PB3", 55),
    _bidirectional("PB4", 56),
    _bidirectional("PB5", 57),
    _bidirectional("PB6", 58),
    _bidirectional("PB7", 59),
    _digital_input("BOOT0", 60),
    _bidirectional("PB8", 61),
    _bidirectional("PB9", 62),
    _ground("VSS", 63),
    _power_input("VDD", 64),
)

STM32F405RGTx = LIB.component(
    "STM32F405RGTx",
    pins=STM32F405RGTx_PINS,
    properties={"category": "mcu", "package": "LQFP-64"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="STMicroelectronics",
        part_number="STM32F405RGT6",
        package="LQFP-64",
        footprint=("Package_QFP", "LQFP-64_10x10mm_P0.5mm"),
    ),
    prefix="U",
    schematic_symbol=_stm32_symbol("STM32F405RGTx", STM32F405RGTx_PINS),
)

USB_B_MICRO = LIB.component(
    "USB_B_Micro",
    pins=[
        _power_input("VBUS", 1),
        _bidirectional("D-", 2),
        _bidirectional("D+", 3),
        PinSpec("ID", 4, role="no_connect", requirement="optional", terminal="signal"),
        _ground("GND", 5),
        _ground("Shield", 6),
    ],
    properties={"category": "connector"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Molex",
        part_number="105017-0001",
        package="Micro-USB-B receptacle",
        footprint=("connectors", "USB_Micro-B_Receptacle"),
    ),
    prefix="J",
    schematic_symbol=_connector_symbol(
        "USB_B_Micro",
        (
            _power_input("VBUS", 1),
            _bidirectional("D-", 2),
            _bidirectional("D+", 3),
            PinSpec("ID", 4, role="no_connect", requirement="optional", terminal="signal"),
            _ground("GND", 5),
            _ground("Shield", 6),
        ),
        "USB",
    ),
)

USBLC6_4SC6 = LIB.component(
    "USBLC6-4SC6",
    pins=[
        _bidirectional("I/O1", 1),
        _ground("GND", 2),
        _bidirectional("I/O2", 3),
        _bidirectional("I/O3", 4),
        _power_input("VBUS", 5),
        _bidirectional("I/O4", 6),
    ],
    properties={"category": "protection"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="STMicroelectronics",
        part_number="USBLC6-4SC6",
        package="SOT-23-6",
        footprint=("Package_TO_SOT_SMD", "SOT-23-6"),
    ),
    prefix="U",
    schematic_symbol=_usb_protection_symbol("USBLC6-4SC6"),
)

AP1117_15 = LIB.component(
    "AP1117-15",
    pins=[
        _ground("GND", 1),
        _power_output("VO", 2),
        _power_input("VI", 3),
    ],
    properties={"category": "regulator"},
    physical_part=PhysicalPartSpec(
        manufacturer="Diodes Incorporated",
        part_number="AP1117E15G-13",
        package="SOT-223-3",
        footprint=("Package_TO_SOT_SMD", "SOT-223-3_TabPin2"),
        pin_pads={1: "1", 2: ("2", "4"), 3: "3"},
    ),
    prefix="U",
    schematic_symbol=_three_pin_regulator_symbol("AP1117_15"),
)

PMOS_DGS = LIB.component(
    "Q_PMOS_DGS",
    pins=[_passive("D", 1), _digital_input("G", 2), _passive("S", 3)],
    properties={"category": "transistor", "benchmark_role": "reverse_polarity_protection"},
    prefix="Q",
)

POLYFUSE = LIB.component(
    "Polyfuse",
    pins=[_passive("1", 1), _passive("2", 2)],
    properties={"category": "protection"},
    prefix="F",
)

FERRITE_BEAD = LIB.component(
    "Ferrite_Bead",
    pins=[_passive("1", 1), _passive("2", 2)],
    properties={"category": "emi"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Murata",
        part_number="BLM18AG601SN1D",
        package="0603",
        footprint=("passives", "L_0603_1608Metric"),
    ),
    prefix="FB",
)

CRYSTAL_GND24 = LIB.component(
    "Crystal_GND24",
    pins=[_passive("1", 1), _ground("GND", 2), _passive("3", 3), _ground("GND", 4)],
    properties={"category": "crystal"},
    prefix="Y",
    schematic_symbol=_crystal_symbol("Crystal_GND24"),
)

SPDT_SWITCH = LIB.component(
    "SW_SPDT",
    pins=[_passive("A", 1), _passive("C", 2), _passive("B", 3)],
    properties={"category": "switch"},
    prefix="SW",
    schematic_symbol=_switch_symbol("SW_SPDT"),
)

JTAG_SWD_10 = LIB.component(
    "Conn_ARM_JTAG_SWD_10",
    pins=[
        _power_input("VTref", 1),
        _bidirectional("SWDIO", 2),
        _ground("GND", 3),
        _digital_input("SWCLK", 4),
        _ground("GND", 5),
        _bidirectional("SWO", 6),
        PinSpec("NC", 7, role="no_connect", requirement="optional", terminal="no_connect"),
        _digital_input("TDI", 8),
        _ground("GNDDetect", 9),
        _digital_input("nRESET", 10),
    ],
    properties={"category": "connector"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Samtec",
        part_number="FTSH-105-01-L-DV-K",
        package="2x05 1.27mm",
        footprint=("connectors", "PinHeader_2x05_P1.27mm_Vertical"),
    ),
    prefix="J",
    schematic_symbol=_connector_symbol(
        "Conn_ARM_JTAG_SWD_10",
        (
            _power_input("VTref", 1),
            _bidirectional("SWDIO", 2),
            _ground("GND", 3),
            _digital_input("SWCLK", 4),
            _ground("GND", 5),
            _bidirectional("SWO", 6),
            PinSpec("NC", 7, role="no_connect", requirement="optional", terminal="no_connect"),
            _digital_input("TDI", 8),
            _ground("GNDDetect", 9),
            _digital_input("nRESET", 10),
        ),
        "SWD",
    ),
)

CONNECTOR_1X04 = LIB.component(
    "Conn_01x04",
    pins=[
        _bidirectional("1", 1),
        _bidirectional("2", 2),
        _bidirectional("3", 3),
        _bidirectional("4", 4),
    ],
    properties={"category": "connector"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Sullins",
        part_number="PEC04SAAN",
        package="1x04 2.54mm",
        footprint=("connectors", "PinHeader_1x04_P2.54mm_Vertical"),
    ),
    prefix="J",
    schematic_symbol=_connector_symbol(
        "Conn_01x04",
        (
            _bidirectional("1", 1),
            _bidirectional("2", 2),
            _bidirectional("3", 3),
            _bidirectional("4", 4),
        ),
        "HDR",
    ),
)

MOUNTING_HOLE_PAD = LIB.component(
    "MountingHole_Pad",
    pins=[_ground("1", 1)],
    properties={"category": "mechanical"},
    prefix="H",
)

DIODE = LIB.component(
    "Diode",
    pins=[_passive("A", 1), _passive("K", 2)],
    properties={"category": "diode"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Diodes Incorporated",
        part_number="1N4148W-7-F",
        package="SOD-123",
        footprint=("diodes", "D_SOD-123"),
    ),
    prefix="D",
    schematic_symbol=_two_pin_symbol("Diode", "D"),
)

ZENER_DIODE = LIB.component(
    "Zener_Diode",
    pins=[_passive("A", 1), _passive("K", 2)],
    properties={"category": "diode"},
    source_name="D_Zener_Small",
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Diodes Incorporated",
        part_number="BZT52C3V3-7-F",
        package="SOD-123",
        footprint=("diodes", "D_SOD-123"),
    ),
    prefix="D",
)

RESISTOR = LIB.component(
    "Resistor",
    pins=[_passive("1", 1), _passive("2", 2)],
    properties={"category": "passive"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Yageo",
        part_number="RC0603FR-0710KL",
        package="0603",
        footprint=("passives", "R_0603_1608Metric"),
        power_rating=0.1,
    ),
    prefix="R",
    schematic_symbol=_two_pin_symbol("Resistor", "R"),
)

CAPACITOR = LIB.component(
    "Capacitor",
    pins=[_passive("1", 1), _passive("2", 2)],
    properties={"category": "passive"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Murata",
        part_number="GRM188R71C104KA01D",
        package="0603",
        footprint=("passives", "C_0603_1608Metric"),
        voltage_rating=16,
    ),
    prefix="C",
    schematic_symbol=_vertical_two_pin_symbol("Capacitor", "C"),
)

INDUCTOR = LIB.component(
    "Inductor",
    pins=[_passive("1", 1), _passive("2", 2)],
    properties={"category": "passive"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="Murata",
        part_number="LQG18HN10NJ00D",
        package="0603",
        footprint=("passives", "L_0603_1608Metric"),
    ),
    prefix="L",
)

__all__ = [
    "AP1117_15",
    "CAPACITOR",
    "CONNECTOR_1X04",
    "CRYSTAL_GND24",
    "DIODE",
    "FERRITE_BEAD",
    "INDUCTOR",
    "JTAG_SWD_10",
    "LIB",
    "MOUNTING_HOLE_PAD",
    "PMOS_DGS",
    "POLYFUSE",
    "RESISTOR",
    "SPDT_SWITCH",
    "STM32F405RGTx",
    "USBLC6_4SC6",
    "USB_B_MICRO",
    "ZENER_DIODE",
]
