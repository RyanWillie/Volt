"""Power-input and regulator logical blocks for the STM32 USB buck example."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib


def _component_properties(value: str, pcb_reference: str) -> dict[str, str]:
    return {"value": value, "pcb_reference": pcb_reference}


def define_power_input_and_regulator(design: volt.Design) -> volt.ModuleDefinition:
    def define(library_component: volt.LibraryComponent) -> volt.ComponentDefinition:
        return design.define_component(
            library_component.name,
            pins=library_component.pins,
            properties=library_component.properties,
            source=(
                lib.LIB.namespace,
                library_component.source_name,
                library_component.source_version,
            ),
            schematic_symbol=library_component.schematic_symbols,
        )

    connector = define(lib.CONNECTOR_1X04)
    buck = define(lib.BUCK_MP2359)
    ldo_3v3 = define(lib.AP1117_33)
    polyfuse = define(lib.POLYFUSE)
    ferrite = define(lib.FERRITE_BEAD)
    inductor = define(lib.POWER_INDUCTOR_10UH)
    schottky = define(lib.SCHOTTKY_B5819)
    cap_10uf = define(lib.CAP_10UF)
    cap_10nf = define(lib.CAP_10NF)
    cap_4u7 = define(lib.CAP_4U7)
    cap_100nf = define(lib.CAP_100NF)
    res_100k = define(lib.RES_100K)
    res_47k = define(lib.RES_47K)
    res_68k = define(lib.RES_68K)
    res_15k = define(lib.RES_15K)

    module = design.define_module("PowerInputAndRegulator")
    input_12v = module.port("IN_12V", kind="power", role="power_input")
    output_5v = module.port("OUT_5V", kind="power", role="power_output")
    output_3v3 = module.port("OUT_3V3", kind="power", role="power_output")
    analog_3v3 = module.port("VDDA", kind="power", role="power_output")
    ground = module.port("GND", kind="ground", role="ground")
    fused_12v = module.net("FUSED_12V", kind="power")
    buck_in = module.net("BUCK_IN", kind="power")
    buck_en = module.net("BUCK_EN")
    buck_switch = module.net("BUCK_SW")
    buck_bootstrap = module.net("BUCK_BST")
    buck_feedback = module.net("BUCK_FB")

    j1 = module.instantiate(connector, ref="J", properties=_component_properties("Power input", "J2"))
    f1 = module.instantiate(polyfuse, ref="F1", properties=_component_properties("250 mA polyfuse", "F1"))
    fb1 = module.instantiate(ferrite, ref="FB1", properties=_component_properties("600 Ohm @ 100 MHz", "FB1"))
    u5 = module.instantiate(buck, ref="U5", properties=_component_properties("MP2359 5V buck", "U2"))
    l1 = module.instantiate(inductor, ref="L1", properties=_component_properties("10 uH", "L1"))
    dsw = module.instantiate(schottky, ref="DSW", properties=_component_properties("B5819W", "D1"))
    c_in = module.instantiate(cap_10uf, ref="CIN", properties=_component_properties("10 uF", "C1"))
    c_boot = module.instantiate(cap_10nf, ref="CBST", properties=_component_properties("10 nF", "C2"))
    c_5v = module.instantiate(cap_10uf, ref="C5V", properties=_component_properties("10 uF", "C3"))
    r_en_top = module.instantiate(res_100k, ref="REN_TOP", properties=_component_properties("100 kOhm", "R1"))
    r_en_bottom = module.instantiate(res_47k, ref="REN_BOT", properties=_component_properties("47 kOhm", "R2"))
    r_fb_top = module.instantiate(res_68k, ref="RFB_TOP", properties=_component_properties("68 kOhm", "R3"))
    r_fb_bottom = module.instantiate(res_15k, ref="RFB_BOT", properties=_component_properties("15 kOhm", "R4"))
    u3v3 = module.instantiate(ldo_3v3, ref="U3V3", properties=_component_properties("AP1117-3.3", "U3"))
    c_3v3 = module.instantiate(cap_4u7, ref="C3V3", properties=_component_properties("4.7 uF", "C4"))
    fb_vdda = module.instantiate(ferrite, ref="FBVDDA", properties=_component_properties("600 Ohm @ 100 MHz", "FB2"))
    c_vdda = module.instantiate(
        cap_100nf,
        ref="CVDDA",
        properties=_component_properties("100 nF", "C5"),
    )

    module.connect(input_12v, j1[1], f1[1])
    fused_12v.connect(f1[2], fb1[1])
    buck_in.connect(fb1[2], u5["IN"], c_in[1], r_en_top[1])
    buck_en.connect(r_en_top[2], r_en_bottom[1], u5["EN"])
    buck_switch.connect(u5["SW"], dsw["K"], l1[1], c_boot[1])
    buck_bootstrap.connect(u5["BST"], c_boot[2])
    module.connect(output_5v, l1[2], c_5v[1], r_fb_top[1], u3v3["VI"])
    buck_feedback.connect(u5["FB"], r_fb_top[2], r_fb_bottom[1])
    module.connect(output_3v3, u3v3["VO"], c_3v3[1], fb_vdda[1])
    module.connect(analog_3v3, fb_vdda[2], c_vdda[1])
    module.connect(
        ground,
        j1[2],
        j1[3],
        j1[4],
        c_in[2],
        u5["GND"],
        r_en_bottom[2],
        dsw["A"],
        c_5v[2],
        r_fb_bottom[2],
        u3v3["GND"],
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
