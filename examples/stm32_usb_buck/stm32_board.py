"""Top-level logical composition for the STM32 USB buck example."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib

from .connection_blocks import (
    add_debug_and_user_connectors,
    add_usb_interface,
    define_usb_interface,
)
from .power_blocks import add_power_input_and_regulator, define_power_input_and_regulator
from .power_nets import PowerNets, create_power_nets
from .utility_blocks import add_led_indicator, define_external_supply, define_led_indicator


def build_design() -> volt.Design:
    design = volt.Design("stm32_usb_buck")
    power = create_power_nets(design)

    external_supply = define_external_supply(design)
    source_12v = design.instantiate(external_supply, ref="VIN_SRC")
    power.input_12v += source_12v["OUT"]
    power.ground += source_12v["GND"]

    power_module = define_power_input_and_regulator(design)
    add_power_input_and_regulator(
        design,
        power_module,
        input_12v=power.input_12v,
        usb_5v=power.usb_5v,
        logic_3v3=power.logic_3v3,
        analog_3v3=power.analog_3v3,
        ground=power.ground,
    )

    usb_dp = design.net("USB_DP")
    usb_dm = design.net("USB_DM")
    mcu_usb_dp = design.net("MCU_USB_DP")
    mcu_usb_dm = design.net("MCU_USB_DM")
    usb_module = define_usb_interface(design)
    add_usb_interface(
        design,
        usb_module,
        vbus=power.usb_5v,
        usb_dp=usb_dp,
        usb_dm=usb_dm,
        mcu_dp=mcu_usb_dp,
        mcu_dm=mcu_usb_dm,
        ground=power.ground,
    )

    mcu_support = define_mcu_support(design)
    support = design.instantiate(mcu_support, ref="SUPPORT")
    reset = design.net("NRST")
    boot0 = design.net("BOOT0")
    hse_in = design.net("HSE_IN")
    hse_out = design.net("HSE_OUT")
    vcap1 = design.net("VCAP_1")
    vcap2 = design.net("VCAP_2")
    power.logic_3v3 += support["VDD"]
    power.ground += support["GND"]
    reset += support["NRST"]
    boot0 += support["BOOT0"]
    hse_in += support["HSE_IN"]
    hse_out += support["HSE_OUT"]
    vcap1 += support["VCAP_1"]
    vcap2 += support["VCAP_2"]

    swdio = design.net("SWDIO").mark_stub()
    swclk = design.net("SWCLK").mark_stub()
    swo = design.net("SWO").mark_stub()
    add_debug_and_user_connectors(
        design,
        logic_3v3=power.logic_3v3,
        ground=power.ground,
        swdio=swdio,
        swclk=swclk,
        swo=swo,
        reset=reset,
        boot0=boot0,
    )

    status_led = design.net("STATUS_LED").mark_stub()
    led_module = define_led_indicator(design)
    add_led_indicator(
        design,
        led_module,
        ref="LED_STATUS",
        supply=power.logic_3v3,
        signal=status_led,
        ground=power.ground,
    )

    mcu = design.instantiate(lib.STM32F405RGTx, ref="U1")
    connect_mcu_power(mcu, power, vcap1=vcap1, vcap2=vcap2)
    mcu_usb_dp += mcu["PA12"]
    mcu_usb_dm += mcu["PA11"]
    reset += mcu["NRST"]
    boot0 += mcu["BOOT0"]
    hse_in += mcu["PH0"]
    hse_out += mcu["PH1"]
    swdio += mcu["PA13"]
    swclk += mcu["PA14"]
    swo += mcu["PB3"]
    status_led += mcu["PC13"]
    mark_unused_mcu_pins_no_connect(mcu)

    mount_holes = [
        design.instantiate(lib.MOUNTING_HOLE_PAD, ref=f"H{index}") for index in range(1, 5)
    ]
    for hole in mount_holes:
        power.ground += hole[1]

    return design


def define_mcu_support(design: volt.Design) -> volt.ModuleDefinition:
    capacitor = design.define_component(
        lib.CAPACITOR.name,
        pins=lib.CAPACITOR.pins,
        properties=lib.CAPACITOR.properties,
        source=(lib.LIB.namespace, lib.CAPACITOR.source_name, lib.CAPACITOR.source_version),
    )
    resistor = design.define_component(
        lib.RESISTOR.name,
        pins=lib.RESISTOR.pins,
        properties=lib.RESISTOR.properties,
        source=(lib.LIB.namespace, lib.RESISTOR.source_name, lib.RESISTOR.source_version),
    )
    crystal = design.define_component(
        lib.CRYSTAL_GND24.name,
        pins=lib.CRYSTAL_GND24.pins,
        properties=lib.CRYSTAL_GND24.properties,
        source=(lib.LIB.namespace, lib.CRYSTAL_GND24.source_name, lib.CRYSTAL_GND24.source_version),
    )
    switch = design.define_component(
        lib.SPDT_SWITCH.name,
        pins=lib.SPDT_SWITCH.pins,
        properties=lib.SPDT_SWITCH.properties,
        source=(lib.LIB.namespace, lib.SPDT_SWITCH.source_name, lib.SPDT_SWITCH.source_version),
    )

    module = design.define_module("McuSupport")
    vdd = module.port("VDD", kind="power", role="power_input")
    ground = module.port("GND", kind="ground", role="ground")
    reset = module.port("NRST", role="input")
    boot0 = module.port("BOOT0", role="input")
    hse_in = module.port("HSE_IN")
    hse_out = module.port("HSE_OUT")
    vcap1 = module.port("VCAP_1", kind="power", role="power_input")
    vcap2 = module.port("VCAP_2", kind="power", role="power_input")

    c_vdd = module.instantiate(capacitor, ref="CVDD")
    c_vcap1 = module.instantiate(capacitor, ref="CVCAP1")
    c_vcap2 = module.instantiate(capacitor, ref="CVCAP2")
    r_reset = module.instantiate(resistor, ref="RRESET")
    r_boot = module.instantiate(resistor, ref="RBOOT")
    sw_boot = module.instantiate(switch, ref="SWBOOT")
    y1 = module.instantiate(crystal, ref="Y1")
    c_hse_in = module.instantiate(capacitor, ref="CHSEIN")
    c_hse_out = module.instantiate(capacitor, ref="CHSEOUT")

    module.connect(vdd, c_vdd[1], r_reset[1], sw_boot["A"])
    module.connect(
        ground,
        c_vdd[2],
        c_vcap1[2],
        c_vcap2[2],
        r_boot[2],
        sw_boot["B"],
        y1.pins("GND"),
        c_hse_in[2],
        c_hse_out[2],
    )
    module.connect(reset, r_reset[2])
    module.connect(boot0, r_boot[1], sw_boot["C"])
    module.connect(hse_in, y1[1], c_hse_in[1])
    module.connect(hse_out, y1[3], c_hse_out[1])
    module.connect(vcap1, c_vcap1[1])
    module.connect(vcap2, c_vcap2[1])
    return module


def connect_mcu_power(
    mcu: volt.Component,
    power: PowerNets,
    *,
    vcap1: volt.Net,
    vcap2: volt.Net,
) -> None:
    power.logic_3v3 += mcu["VBAT"], mcu.pins("VDD")
    power.analog_3v3 += mcu["VDDA"]
    power.ground += mcu.pins("VSS"), mcu["VSSA"]
    vcap1 += mcu["VCAP_1"]
    vcap2 += mcu["VCAP_2"]


def mark_unused_mcu_pins_no_connect(mcu: volt.Component) -> None:
    used = {
        "VBAT",
        "VDD",
        "VSS",
        "VDDA",
        "VSSA",
        "VCAP_1",
        "VCAP_2",
        "PA11",
        "PA12",
        "NRST",
        "BOOT0",
        "PH0",
        "PH1",
        "PA13",
        "PA14",
        "PB3",
        "PC13",
    }
    for pin_number in range(1, 65):
        pin = mcu[pin_number]
        pin_name = lib.STM32F405RGTx_PINS[pin_number - 1].name
        if pin_name not in used:
            pin.mark_no_connect()
