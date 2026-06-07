"""Logical components and selected physical parts for the 555 blinker example."""

from __future__ import annotations

import volt


def _front_smd_pad(
    label: str,
    *,
    at: tuple[float, float],
    size: tuple[float, float],
    shape: str = "rounded_rectangle",
    mechanical_role: str | None = None,
) -> volt.FootprintPad:
    return volt.FootprintPad.surface_mount(
        label,
        at=at,
        size=size,
        shape=shape,
        mechanical_role=mechanical_role,
    )


TIMER_SYMBOL = volt.SchematicSymbolSpec.ic(
    "volt.examples.timer_555_led_blinker:NE555",
    pins=(
        volt.SchematicSymbolSpec.ic_pin("DISCH", 7, side="left", slot=1, label="DIS"),
        volt.SchematicSymbolSpec.ic_pin("THRESH", 6, side="left", slot=2, label="THR"),
        volt.SchematicSymbolSpec.ic_pin("TRIG", 2, side="left", slot=3, label="TRG"),
        volt.SchematicSymbolSpec.ic_pin("OUT", 3, side="right", slot=2),
        volt.SchematicSymbolSpec.ic_pin("CTRL", 5, side="right", slot=3, label="CTL"),
        volt.SchematicSymbolSpec.ic_pin("RESET", 4, side="top", slot=2, label="RST"),
        volt.SchematicSymbolSpec.ic_pin("VCC", 8, side="top", slot=4, label="Vcc"),
        volt.SchematicSymbolSpec.ic_pin("GND", 1, side="bottom", slot=3),
    ),
    center_label="555",
    pin_numbers=True,
)

SUPPLY_SYMBOL = volt.SchematicSymbolSpec(
    "volt.examples.timer_555_led_blinker:ExternalSupply",
    pins=(
        volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),
        volt.SchematicSymbolSpec.pin("2", 2, (0, 8), "Left"),
    ),
    primitives=(
        volt.SchematicSymbolSpec.rectangle((8, -4), (22, 12)),
        volt.SchematicSymbolSpec.text("J", (15, -10)),
        volt.SchematicSymbolSpec.line((0, 0), (8, 0)),
        volt.SchematicSymbolSpec.line((0, 8), (8, 8)),
    ),
)

FOOTPRINTS = {
    "jst_ph_smd_1x02": volt.FootprintDefinition(
        ("Connector_JST", "JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal"),
        pads=(
            _front_smd_pad("1", at=(-1.0, -2.85), size=(1.0, 3.5)),
            _front_smd_pad("2", at=(1.0, -2.85), size=(1.0, 3.5)),
            _front_smd_pad(
                "MP1",
                at=(-3.35, 2.9),
                size=(1.5, 3.4),
                mechanical_role="mechanical_support",
            ),
            _front_smd_pad(
                "MP2",
                at=(3.35, 2.9),
                size=(1.5, 3.4),
                mechanical_role="mechanical_support",
            ),
        ),
    ),
    "timer_soic_8": volt.FootprintDefinition(
        ("KiCad_Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm"),
        pads=(
            _front_smd_pad("1", at=(-2.475, -1.905), size=(1.95, 0.6)),
            _front_smd_pad("2", at=(-2.475, -0.635), size=(1.95, 0.6)),
            _front_smd_pad("3", at=(-2.475, 0.635), size=(1.95, 0.6)),
            _front_smd_pad("4", at=(-2.475, 1.905), size=(1.95, 0.6)),
            _front_smd_pad("5", at=(2.475, 1.905), size=(1.95, 0.6)),
            _front_smd_pad("6", at=(2.475, 0.635), size=(1.95, 0.6)),
            _front_smd_pad("7", at=(2.475, -0.635), size=(1.95, 0.6)),
            _front_smd_pad("8", at=(2.475, -1.905), size=(1.95, 0.6)),
        ),
    ),
    "resistor_0805": volt.FootprintDefinition(
        ("Resistor_SMD", "R_0805_2012Metric"),
        pads=(
            _front_smd_pad("1", at=(-0.9125, 0.0), size=(1.025, 1.4)),
            _front_smd_pad("2", at=(0.9125, 0.0), size=(1.025, 1.4)),
        ),
    ),
    "capacitor_0805": volt.FootprintDefinition(
        ("Capacitor_SMD", "C_0805_2012Metric"),
        pads=(
            _front_smd_pad("1", at=(-0.95, 0.0), size=(1.0, 1.45)),
            _front_smd_pad("2", at=(0.95, 0.0), size=(1.0, 1.45)),
        ),
    ),
    "led_0805": volt.FootprintDefinition(
        ("LED_SMD", "LED_0805_2012Metric"),
        pads=(
            _front_smd_pad("1", at=(-0.9375, 0.0), size=(0.975, 1.4)),
            _front_smd_pad("2", at=(0.9375, 0.0), size=(0.975, 1.4)),
        ),
    ),
}


def build_design() -> tuple[volt.Design, dict[str, volt.Net], dict[str, volt.Component]]:
    design = volt.Design("timer-555-led-blinker")
    supply_definition = design.define_component(
        "ExternalSupply",
        source=("volt.examples.timer_555_led_blinker", "external_supply", "1.0.0"),
        pins=[
            volt.PinSpec("1", 1, role="power_output"),
            volt.PinSpec("2", 2, role="ground"),
        ],
        properties={"category": "validation_source"},
        schematic_symbol=SUPPLY_SYMBOL,
    )
    timer_definition = design.define_component(
        "NE555",
        source=("volt.examples.timer_555_led_blinker", "ne555", "1.0.0"),
        pins=[
            volt.PinSpec("GND", 1, role="ground"),
            volt.PinSpec("TRIG", 2, role="analog_input"),
            volt.PinSpec("OUT", 3, role="output", signal="digital"),
            volt.PinSpec("RESET", 4, role="input", signal="digital"),
            volt.PinSpec("CTRL", 5, role="analog_input"),
            volt.PinSpec("THRESH", 6, role="analog_input"),
            volt.PinSpec("DISCH", 7, role="analog_output"),
            volt.PinSpec("VCC", 8, role="power"),
        ],
        schematic_symbol=TIMER_SYMBOL,
    )

    nets = {
        "+5V": design.net("+5V", kind="power", voltage=5.0),
        "GND": design.net("GND", kind="ground"),
        "DISCH": design.net("DISCH"),
        "TIMING": design.net("TIMING"),
        "CTRL": design.net("CTRL"),
        "OUT": design.net("OUT"),
        "LED_A": design.net("LED_A"),
    }
    parts = {
        "J1": design.instantiate(supply_definition, ref="J1"),
        "U1": design.instantiate(timer_definition, ref="U1", properties={"value": "NE555"}),
        "RA": design.R("100 kOhm", ref="R1"),
        "RB": design.R("47 kOhm", ref="R2"),
        "CT": design.C("1 uF", ref="C1"),
        "CCTRL": design.C("10 nF", ref="C2"),
        "CDEC": design.C("100 nF", ref="C3"),
        "RLED": design.R("1 kOhm", ref="R3"),
        "DLED": design.LED(ref="D1"),
    }

    timer = parts["U1"]
    nets["+5V"] += (
        parts["J1"][1],
        timer["VCC"],
        timer["RESET"],
        parts["RA"][1],
        parts["CDEC"][1],
    )
    nets["DISCH"] += timer["DISCH"], parts["RA"][2], parts["RB"][1]
    nets["TIMING"] += timer["TRIG"], timer["THRESH"], parts["RB"][2], parts["CT"][1]
    nets["CTRL"] += timer["CTRL"], parts["CCTRL"][1]
    nets["OUT"] += timer["OUT"], parts["RLED"][1]
    nets["LED_A"] += parts["RLED"][2], parts["DLED"]["A"]
    nets["GND"] += (
        parts["J1"][2],
        timer["GND"],
        parts["CT"][2],
        parts["CCTRL"][2],
        parts["CDEC"][2],
        parts["DLED"]["K"],
    )

    parts["J1"].select_part(
        manufacturer="JST",
        part_number="S2B-PH-SM4-TB(LF)(SN)",
        package="JST-PH-SMD-1x02",
        footprint=FOOTPRINTS["jst_ph_smd_1x02"],
        pin_pads={1: "1", 2: "2"},
    )
    timer.select_part(
        manufacturer="Texas Instruments",
        part_number="NE555DR",
        package="SOIC-8",
        footprint=FOOTPRINTS["timer_soic_8"],
        pin_pads={
            "GND": "1",
            "TRIG": "2",
            "OUT": "3",
            "RESET": "4",
            "CTRL": "5",
            "THRESH": "6",
            "DISCH": "7",
            "VCC": "8",
        },
        voltage_rating=16.0,
    )
    for component, part_number, power_rating in (
        (parts["RA"], "RMCF0805FT100K", 0.125),
        (parts["RB"], "RMCF0805FT47K0", 0.125),
        (parts["RLED"], "RMCF0805FT1K00", 0.125),
    ):
        component.select_part(
            manufacturer="Stackpole",
            part_number=part_number,
            package="0805",
            footprint=FOOTPRINTS["resistor_0805"],
            pin_pads={1: "1", 2: "2"},
            power_rating=power_rating,
        )
    for component, part_number, voltage_rating in (
        (parts["CT"], "CL21B105KBFNNNE", 50.0),
        (parts["CCTRL"], "CL21B103KBANNNC", 50.0),
        (parts["CDEC"], "CL21B104KBCNNNC", 50.0),
    ):
        component.select_part(
            manufacturer="Samsung Electro-Mechanics",
            part_number=part_number,
            package="0805",
            footprint=FOOTPRINTS["capacitor_0805"],
            pin_pads={1: "1", 2: "2"},
            voltage_rating=voltage_rating,
        )
    parts["DLED"].select_part(
        manufacturer="Lite-On",
        part_number="LTST-C171KRKT",
        package="0805",
        footprint=FOOTPRINTS["led_0805"],
        pin_pads={"K": "1", "A": "2"},
    )
    return design, nets, parts
