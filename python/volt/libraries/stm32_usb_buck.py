"""Native component library for the STM32 USB buck benchmark."""

from __future__ import annotations

from volt import Library, PhysicalPartSpec, PinSpec

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


STM32F405RGTx = LIB.component(
    "STM32F405RGTx",
    pins=[
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
    ],
    properties={"category": "mcu", "package": "LQFP-64"},
    physical_part=PhysicalPartSpec.same_numbered(
        manufacturer="STMicroelectronics",
        part_number="STM32F405RGT6",
        package="LQFP-64",
        footprint=("Package_QFP", "LQFP-64_10x10mm_P0.5mm"),
    ),
    prefix="U",
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
    prefix="J",
)

USBLC6_4SC6 = LIB.component(
    "USBLC6-4SC6",
    pins=[
        _bidirectional("I/O1", 1),
        _ground("GND", 2),
        _bidirectional("I/O1", 3),
        _bidirectional("I/O2", 4),
        _power_input("VBUS", 5),
        _bidirectional("I/O2", 6),
    ],
    properties={"category": "protection"},
    prefix="U",
)

AP1117_15 = LIB.component(
    "AP1117-15",
    pins=[
        _ground("GND", 1),
        _power_output("VO", 2),
        _power_input("VI", 3),
    ],
    properties={"category": "regulator"},
    prefix="U",
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
    prefix="FB",
)

CRYSTAL_GND24 = LIB.component(
    "Crystal_GND24",
    pins=[_passive("1", 1), _ground("GND", 2), _passive("3", 3), _ground("GND", 4)],
    properties={"category": "crystal"},
    prefix="Y",
)

SPDT_SWITCH = LIB.component(
    "SW_SPDT",
    pins=[_passive("A", 1), _passive("C", 2), _passive("B", 3)],
    properties={"category": "switch"},
    prefix="SW",
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
        PinSpec("NC", 8, role="no_connect", requirement="optional", terminal="no_connect"),
        _ground("GNDDetect", 9),
        _digital_input("nRESET", 10),
    ],
    properties={"category": "connector"},
    prefix="J",
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
    prefix="J",
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
    prefix="D",
)

ZENER_DIODE = LIB.component(
    "Zener_Diode",
    pins=[_passive("A", 1), _passive("K", 2)],
    properties={"category": "diode"},
    source_name="D_Zener_Small",
    prefix="D",
)

RESISTOR = LIB.component(
    "Resistor",
    pins=[_passive("1", 1), _passive("2", 2)],
    properties={"category": "passive"},
    prefix="R",
)

CAPACITOR = LIB.component(
    "Capacitor",
    pins=[_passive("1", 1), _passive("2", 2)],
    properties={"category": "passive"},
    prefix="C",
)

INDUCTOR = LIB.component(
    "Inductor",
    pins=[_passive("1", 1), _passive("2", 2)],
    properties={"category": "passive"},
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
