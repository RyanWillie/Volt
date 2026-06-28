"""Connector and USB logical blocks for the STM32 USB buck example."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib


def define_usb_interface(design: volt.Design) -> volt.ModuleDefinition:
    usb_connector = design.define_component(
        lib.USB_B_MICRO.name,
        pins=lib.USB_B_MICRO.pins,
        properties=lib.USB_B_MICRO.properties,
        source=(lib.LIB.namespace, lib.USB_B_MICRO.source_name, lib.USB_B_MICRO.source_version),
    )
    protection = design.define_component(
        lib.USBLC6_4SC6.name,
        pins=lib.USBLC6_4SC6.pins,
        properties=lib.USBLC6_4SC6.properties,
        source=(lib.LIB.namespace, lib.USBLC6_4SC6.source_name, lib.USBLC6_4SC6.source_version),
    )

    module = design.define_module("UsbInterface")
    vbus = module.port("VBUS", kind="power", role="power_input")
    usb_dp = module.port("USB_DP", role="bidirectional")
    usb_dm = module.port("USB_DM", role="bidirectional")
    mcu_dp = module.port("MCU_USB_DP", role="bidirectional")
    mcu_dm = module.port("MCU_USB_DM", role="bidirectional")
    ground = module.port("GND", kind="ground", role="ground")

    connector = module.instantiate(usb_connector, ref="J1", properties={"value": "USB Micro-B"})
    protector = module.instantiate(protection, ref="U1", properties={"value": "USBLC6-4SC6"})

    module.connect(vbus, connector["VBUS"], protector["VBUS"])
    module.connect(usb_dp, connector["D+"], protector["I/O1"])
    module.connect(usb_dm, connector["D-"], protector["I/O2"])
    module.connect(mcu_dp, protector["I/O4"])
    module.connect(mcu_dm, protector["I/O3"])
    module.connect(ground, connector["GND"], connector["Shield"], protector["GND"])
    return module


def add_usb_interface(
    design: volt.Design,
    module: volt.ModuleDefinition,
    *,
    vbus: volt.Net,
    usb_dp: volt.Net,
    usb_dm: volt.Net,
    mcu_dp: volt.Net,
    mcu_dm: volt.Net,
    ground: volt.Net,
) -> volt.ModuleInstance:
    instance = design.instantiate(module, ref="USB")
    vbus += instance["VBUS"]
    usb_dp += instance["USB_DP"]
    usb_dm += instance["USB_DM"]
    mcu_dp += instance["MCU_USB_DP"]
    mcu_dm += instance["MCU_USB_DM"]
    ground += instance["GND"]
    instance.component("J1")["ID"].mark_no_connect()
    return instance


def add_debug_and_user_connectors(
    design: volt.Design,
    *,
    logic_3v3: volt.Net,
    ground: volt.Net,
    swdio: volt.Net,
    swclk: volt.Net,
    swo: volt.Net,
    reset: volt.Net,
    boot0: volt.Net,
) -> tuple[volt.Component, volt.Component]:
    swd = design.instantiate(lib.JTAG_SWD_10, ref="J2", properties={"value": "SWD 10-pin"})
    gpio = design.instantiate(lib.CONNECTOR_1X04, ref="J3", properties={"value": "GPIO 1x4"})

    logic_3v3 += swd["VTref"], gpio[1]
    swdio += swd["SWDIO"]
    swclk += swd["SWCLK"]
    swo += swd["SWO"]
    reset += swd["nRESET"]
    boot0 += swd["TDI"], gpio[2]
    ground += swd.pins("GND"), swd["GNDDetect"], gpio[4]
    gpio[3].mark_no_connect()
    swd["NC"].mark_no_connect()
    return swd, gpio
