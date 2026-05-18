"""Power-input and regulator logical blocks for the STM32 USB buck example."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib


def define_power_input_and_regulator(design: volt.Design) -> volt.ModuleDefinition:
    connector = design.define_component(
        lib.CONNECTOR_1X04.name,
        pins=lib.CONNECTOR_1X04.pins,
        properties=lib.CONNECTOR_1X04.properties,
        source=(
            lib.LIB.namespace,
            lib.CONNECTOR_1X04.source_name,
            lib.CONNECTOR_1X04.source_version,
        ),
    )
    regulator = design.define_component(
        lib.AP1117_15.name,
        pins=lib.AP1117_15.pins,
        properties=lib.AP1117_15.properties,
        source=(lib.LIB.namespace, lib.AP1117_15.source_name, lib.AP1117_15.source_version),
    )
    passive = design.define_component(
        lib.CAPACITOR.name,
        pins=lib.CAPACITOR.pins,
        properties=lib.CAPACITOR.properties,
        source=(lib.LIB.namespace, lib.CAPACITOR.source_name, lib.CAPACITOR.source_version),
    )

    module = design.define_module("PowerInputAndRegulator")
    input_12v = module.port("IN_12V", kind="power", role="power_input")
    output_5v = module.port("OUT_5V", kind="power", role="power_output")
    output_3v3 = module.port("OUT_3V3", kind="power", role="power_output")
    analog_3v3 = module.port("VDDA", kind="power", role="power_output")
    ground = module.port("GND", kind="ground", role="ground")

    j1 = module.instantiate(connector, ref="J", properties={"value": "Power input"})
    u5 = module.instantiate(regulator, ref="U5", properties={"value": "AP1117-5.0"})
    u3v3 = module.instantiate(regulator, ref="U3V3", properties={"value": "AP1117-3.3"})
    c_in = module.instantiate(passive, ref="CIN", properties={"value": "4.7 uF"})
    c_5v = module.instantiate(passive, ref="C5V", properties={"value": "4.7 uF"})
    c_3v3 = module.instantiate(passive, ref="C3V3", properties={"value": "4.7 uF"})
    c_vdda = module.instantiate(passive, ref="CVDDA", properties={"value": "100 nF"})

    module.connect(input_12v, j1[1], u5["VI"], c_in[1])
    module.connect(output_5v, u5["VO"], u3v3["VI"], c_5v[1])
    module.connect(output_3v3, u3v3["VO"], c_3v3[1])
    module.connect(analog_3v3, c_vdda[1])
    module.connect(
        ground,
        j1[2],
        j1[3],
        j1[4],
        u5["GND"],
        u3v3["GND"],
        c_in[2],
        c_5v[2],
        c_3v3[2],
        c_vdda[2],
    )
    return module


def add_power_input_and_regulator(
    design: volt.Design,
    module: volt.ModuleDefinition,
    *,
    input_12v: volt.Net,
    usb_5v: volt.Net,
    logic_3v3: volt.Net,
    analog_3v3: volt.Net,
    ground: volt.Net,
) -> volt.ModuleInstance:
    instance = design.instantiate(module, ref="PWR")
    input_12v += instance["IN_12V"]
    usb_5v += instance["OUT_5V"]
    logic_3v3 += instance["OUT_3V3"]
    analog_3v3 += instance["VDDA"]
    ground += instance["GND"]
    return instance
