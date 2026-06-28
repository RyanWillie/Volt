"""Top-level logical composition for the STM32 USB buck example."""

from __future__ import annotations

from dataclasses import dataclass

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


STUB_NET_NAMES = ("SWDIO", "SWCLK", "SWO", "STATUS_LED")

STM32_USED_PIN_NAMES = frozenset(
    {
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
)
STM32_UNUSED_PIN_NO_CONNECTS = tuple(
    f"U1.{pin.name}"
    for pin in lib.STM32F405RGTx_PINS
    if pin.name not in STM32_USED_PIN_NAMES
)
NO_CONNECT_PIN_LABELS = (
    "USB/J1.ID",
    "J2.NC",
    "J3.3",
    *STM32_UNUSED_PIN_NO_CONNECTS,
)


@dataclass(frozen=True)
class Stm32UsbBuckBoard:
    design: volt.Design
    power: PowerNets
    nets: dict[str, volt.Net]
    modules: dict[str, volt.ModuleInstance]
    components: dict[str, volt.Component]


def build_board() -> Stm32UsbBuckBoard:
    design = volt.Design("stm32_usb_buck")
    power = create_power_nets(design)
    nets = {
        "+12V": power.input_12v,
        "+5V": power.usb_5v,
        "+3V3": power.logic_3v3,
        "VDDA": power.analog_3v3,
        "GND": power.ground,
    }
    modules: dict[str, volt.ModuleInstance] = {}
    components: dict[str, volt.Component] = {}

    external_supply = define_external_supply(design)
    source_12v = design.instantiate(external_supply, ref="VIN_SRC", properties={"value": "12 V input"})
    _select_external_supply_part(source_12v)
    components["VIN_SRC"] = source_12v
    power.input_12v += source_12v["OUT"]
    power.ground += source_12v["GND"]

    power_module = define_power_input_and_regulator(design)
    power_instance = add_power_input_and_regulator(
        design,
        power_module,
        input_12v=power.input_12v,
        usb_5v=power.usb_5v,
        logic_3v3=power.logic_3v3,
        analog_3v3=power.analog_3v3,
        ground=power.ground,
    )
    _select_module_library_parts(
        power_instance,
        {
            "J": lib.CONNECTOR_1X04,
            "U5": lib.AP1117_15,
            "U3V3": lib.AP1117_15,
            "CIN": lib.CAPACITOR,
            "C5V": lib.CAPACITOR,
            "C3V3": lib.CAPACITOR,
            "CVDDA": lib.CAPACITOR,
        },
    )
    modules["PWR"] = power_instance

    usb_dp = design.net("USB_DP")
    usb_dm = design.net("USB_DM")
    mcu_usb_dp = design.net("MCU_USB_DP")
    mcu_usb_dm = design.net("MCU_USB_DM")
    nets.update(
        {
            "USB_DP": usb_dp,
            "USB_DM": usb_dm,
            "MCU_USB_DP": mcu_usb_dp,
            "MCU_USB_DM": mcu_usb_dm,
        }
    )
    usb_module = define_usb_interface(design)
    usb_instance = add_usb_interface(
        design,
        usb_module,
        vbus=power.usb_5v,
        usb_dp=usb_dp,
        usb_dm=usb_dm,
        mcu_dp=mcu_usb_dp,
        mcu_dm=mcu_usb_dm,
        ground=power.ground,
    )
    _select_module_library_parts(
        usb_instance,
        {
            "J1": lib.USB_B_MICRO,
            "U1": lib.USBLC6_4SC6,
        },
    )
    modules["USB"] = usb_instance
    mcu_support = define_mcu_support(design)
    support = design.instantiate(mcu_support, ref="SUPPORT")
    _select_module_library_parts(
        support,
        {
            "CVDD": lib.CAPACITOR,
            "CVCAP1": lib.CAPACITOR,
            "CVCAP2": lib.CAPACITOR,
            "RRESET": lib.RESISTOR,
            "RBOOT": lib.RESISTOR,
            "SWBOOT": lib.SPDT_SWITCH,
            "Y1": lib.CRYSTAL_GND24,
            "CHSEIN": lib.CAPACITOR,
            "CHSEOUT": lib.CAPACITOR,
        },
    )
    modules["SUPPORT"] = support
    reset = design.net("NRST")
    boot0 = design.net("BOOT0")
    hse_in = design.net("HSE_IN")
    hse_out = design.net("HSE_OUT")
    vcap1 = design.net("VCAP_1")
    vcap2 = design.net("VCAP_2")
    nets.update(
        {
            "NRST": reset,
            "BOOT0": boot0,
            "HSE_IN": hse_in,
            "HSE_OUT": hse_out,
            "VCAP_1": vcap1,
            "VCAP_2": vcap2,
        }
    )
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
    nets.update({"SWDIO": swdio, "SWCLK": swclk, "SWO": swo})
    swd, gpio = add_debug_and_user_connectors(
        design,
        logic_3v3=power.logic_3v3,
        ground=power.ground,
        swdio=swdio,
        swclk=swclk,
        swo=swo,
        reset=reset,
        boot0=boot0,
    )
    components["J2"] = swd
    components["J3"] = gpio

    status_led = design.net("STATUS_LED").mark_stub()
    nets["STATUS_LED"] = status_led
    led_module = define_led_indicator(design)
    led_instance = add_led_indicator(
        design,
        led_module,
        ref="LED_STATUS",
        supply=power.logic_3v3,
        signal=status_led,
        ground=power.ground,
    )
    _select_module_library_parts(led_instance, {"R": lib.RESISTOR})
    _select_indicator_led_part(led_instance.component("D"))
    modules["LED_STATUS"] = led_instance

    mcu = design.instantiate(lib.STM32F405RGTx, ref="U1", properties={"value": "STM32F405RGT6"})
    components["U1"] = mcu
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
    for index, hole in enumerate(mount_holes, start=1):
        power.ground += hole[1]
        components[f"H{index}"] = hole

    for component in design.components():
        component.dnp(False)
    source_12v.dnp(True)
    for hole in mount_holes:
        hole.dnp(True)

    return Stm32UsbBuckBoard(
        design=design,
        power=power,
        nets=nets,
        modules=modules,
        components=components,
    )


def build_design() -> volt.Design:
    return build_board().design


def _select_module_library_parts(
    instance: volt.ModuleInstance,
    parts_by_ref: dict[str, volt.LibraryComponent],
) -> None:
    for reference, library_component in parts_by_ref.items():
        _select_library_physical_part(instance.component(reference), library_component)


def _select_library_physical_part(
    component: volt.Component,
    library_component: volt.LibraryComponent,
) -> None:
    physical_part = library_component.physical_part
    if physical_part is None:
        raise ValueError(f"{library_component.name} does not define a physical part")
    component.select_part(
        manufacturer=physical_part.manufacturer,
        part_number=physical_part.part_number,
        package=physical_part.package,
        footprint=physical_part.footprint,
        pin_pads=physical_part.pin_pads_for(library_component),
        properties=physical_part.properties,
        voltage_rating=physical_part.voltage_rating,
        power_rating=physical_part.power_rating,
        model_3d=physical_part.model_3d,
        approved_alternate_mpns=physical_part.approved_alternate_mpns,
    )


def _select_external_supply_part(component: volt.Component) -> None:
    component.select_part(
        manufacturer="Generic",
        part_number="HDR-1x02-2.54mm",
        package="1x02 2.54mm header",
        footprint=lib.HEADER_1X02_FOOTPRINT,
        pin_pads={1: "1", 2: "2"},
    )


def _select_indicator_led_part(component: volt.Component) -> None:
    component.select_part(
        manufacturer="Lite-On",
        part_number="LTST-C190KGKT",
        package="0603 LED",
        footprint=lib.LED_0603_FOOTPRINT,
        pin_pads={"A": "1", "K": "2"},
    )


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

    c_vdd = module.instantiate(
        capacitor,
        ref="CVDD",
        properties={"value": "100 nF", "pcb_reference": "C5"},
    )
    c_vcap1 = module.instantiate(
        capacitor,
        ref="CVCAP1",
        properties={"value": "2.2 uF", "pcb_reference": "C6"},
    )
    c_vcap2 = module.instantiate(
        capacitor,
        ref="CVCAP2",
        properties={"value": "2.2 uF", "pcb_reference": "C7"},
    )
    r_reset = module.instantiate(
        resistor,
        ref="RRESET",
        properties={"value": "10 kOhm", "pcb_reference": "R1"},
    )
    r_boot = module.instantiate(
        resistor,
        ref="RBOOT",
        properties={"value": "100 kOhm", "pcb_reference": "R2"},
    )
    sw_boot = module.instantiate(
        switch,
        ref="SWBOOT",
        properties={"value": "BOOT0", "pcb_reference": "SW1"},
    )
    y1 = module.instantiate(crystal, ref="Y1", properties={"value": "8 MHz", "pcb_reference": "Y1"})
    c_hse_in = module.instantiate(
        capacitor,
        ref="CHSEIN",
        properties={"value": "18 pF", "pcb_reference": "C8"},
    )
    c_hse_out = module.instantiate(
        capacitor,
        ref="CHSEOUT",
        properties={"value": "18 pF", "pcb_reference": "C9"},
    )

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
    for pin_number in range(1, 65):
        pin = mcu[pin_number]
        pin_name = lib.STM32F405RGTx_PINS[pin_number - 1].name
        if pin_name not in STM32_USED_PIN_NAMES:
            pin.mark_no_connect()
