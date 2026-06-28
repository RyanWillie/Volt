"""Small reusable logical blocks for the STM32 USB buck example."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib


def define_external_supply(design: volt.Design) -> volt.ComponentDefinition:
    return design.define_component(
        "ExternalSupply",
        pins=[
            volt.PinSpec("OUT", 1, role="power_output"),
            volt.PinSpec("GND", 2, role="ground"),
        ],
        properties={"category": "validation_source"},
        source=("volt.examples.stm32_usb_buck", "external_supply", "1.0.0"),
    )


def define_led_indicator(design: volt.Design) -> volt.ModuleDefinition:
    resistor = design.define_component(
        lib.RES_330R.name,
        pins=lib.RES_330R.pins,
        properties=lib.RES_330R.properties,
        source=(lib.LIB.namespace, lib.RES_330R.source_name, lib.RES_330R.source_version),
        schematic_symbol=lib.RES_330R.schematic_symbols,
    )
    led = design.define_component(
        "IndicatorLED",
        pins=[
            volt.PinSpec("A", 1, role="passive", terminal="passive", direction="passive"),
            volt.PinSpec("K", 2, role="passive", terminal="passive", direction="passive"),
        ],
        properties={"category": "indicator"},
        source=("volt.examples.stm32_usb_buck", "indicator_led", "1.0.0"),
    )

    module = design.define_module("LedIndicator")
    supply = module.port("SUPPLY", kind="power", role="power_input")
    signal = module.port("SIGNAL", role="input")
    led_anode = module.net("LED_A")
    r1 = module.instantiate(
        resistor,
        ref="R",
        properties={"value": "330 Ohm", "pcb_reference": "R7"},
    )
    d1 = module.instantiate(
        led,
        ref="D",
        properties={"value": "Green LED", "pcb_reference": "D2"},
    )

    module.connect(supply, r1[1])
    led_anode.connect(r1[2], d1["A"])
    module.connect(signal, d1["K"])
    return module


def add_led_indicator(
    design: volt.Design,
    module: volt.ModuleDefinition,
    *,
    ref: str,
    supply: volt.Net,
    signal: volt.Net,
) -> volt.ModuleInstance:
    instance = design.instantiate(module, ref=ref)
    supply += instance["SUPPLY"]
    signal += instance["SIGNAL"]
    return instance
