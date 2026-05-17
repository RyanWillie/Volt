"""Schematic projection for the STM32 USB buck benchmark."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib

from .stm32_board import Stm32UsbBuckBoard

# Use the generic two-terminal symbols so drawing.C/R can orient the passives.
TWO_TERMINAL_CAPACITOR = "capacitor"
TWO_TERMINAL_RESISTOR = "resistor"


def _external_supply_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:ExternalSupply",
        pins=(
            volt.SchematicSymbolSpec.pin("OUT", 1, (44, 8), "Right"),
            volt.SchematicSymbolSpec.pin("GND", 2, (44, 24), "Right"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.rectangle((0, 0), (34, 32)),
            volt.SchematicSymbolSpec.line((34, 8), (44, 8)),
            volt.SchematicSymbolSpec.line((34, 24), (44, 24)),
            volt.SchematicSymbolSpec.text("VIN", (17, 17)),
        ),
    )


def _indicator_led_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:IndicatorLED",
        pins=(
            volt.SchematicSymbolSpec.pin("A", 1, (0, 0), "Left"),
            volt.SchematicSymbolSpec.pin("K", 2, (24, 0), "Right"),
        ),
        primitives=(
            volt.SchematicSymbolSpec.line((0, 0), (8, 0)),
            volt.SchematicSymbolSpec.line((16, 0), (24, 0)),
            volt.SchematicSymbolSpec.line((8, -6), (8, 6)),
            volt.SchematicSymbolSpec.line((8, -6), (16, 0)),
            volt.SchematicSymbolSpec.line((8, 6), (16, 0)),
            volt.SchematicSymbolSpec.line((16, -6), (16, 6)),
            volt.SchematicSymbolSpec.text("LED", (12, -10)),
        ),
    )


def build_schematic(board: Stm32UsbBuckBoard) -> volt.Schematic:
    """Create a deterministic schematic projection for the benchmark board."""

    nets = {net.name: net for net in board.design.nets()}
    net_by_pin = {
        pin.index: net
        for net in board.design.nets()
        for pin in net.pins()
    }

    power = board.design.schematic("Power")
    mcu = board.design.schematic("MCU")
    connectors = board.design.schematic("USB and Connectors")

    _author_power_sheet(power, board, nets, net_by_pin)
    _author_usb_sheet(connectors, board, nets, net_by_pin)
    _author_mcu_sheet(mcu, board, nets, net_by_pin)
    return power


def _author_power_sheet(
    sheet: volt.Schematic,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
    net_by_pin: dict[int, volt.Net],
) -> None:
    pwr = board.modules["PWR"]

    with sheet.drawing(unit=20) as drawing:
        _rail_legend(drawing, nets, (222, 18), ("+12V", "+5V", "+3V3", "VDDA", "GND"))

        vin = drawing.place(
            board.components["VIN_SRC"],
            at=(14, 24),
            symbol=_external_supply_symbol(),
        )
        pwr_j = drawing.place(
            pwr.component("J"),
            at=(16, 78),
            symbol=lib.CONNECTOR_1X04.schematic_symbol,
        )
        u5 = drawing.place(pwr.component("U5"), at=(76, 42), symbol=lib.AP1117_15.schematic_symbol)
        u3v3 = drawing.place(
            pwr.component("U3V3"),
            at=(154, 42),
            symbol=lib.AP1117_15.schematic_symbol,
        )
        cin = drawing.C(pwr.component("CIN"), symbol=TWO_TERMINAL_CAPACITOR).at((60, 118)).down()
        c5v = drawing.C(pwr.component("C5V"), symbol=TWO_TERMINAL_CAPACITOR).at((118, 118)).down()
        c3v3 = drawing.C(pwr.component("C3V3"), symbol=TWO_TERMINAL_CAPACITOR).at(
            (176, 118)
        ).down()
        cvdda = drawing.C(pwr.component("CVDDA"), symbol=TWO_TERMINAL_CAPACITOR).at(
            (234, 118)
        ).down()

        _rail(drawing, nets["+12V"], vin.OUT, orient="Right")
        _rail(drawing, nets["GND"], vin.GND, orient="Down")

        pwr_in = nets["PWR/IN_12V"]
        pwr_5v = nets["PWR/OUT_5V"]
        pwr_3v3 = nets["PWR/OUT_3V3"]
        pwr_vdda = nets["PWR/VDDA"]
        pwr_gnd = nets["PWR/GND"]

        drawing.connect(pwr_j[1], u5.VI, net=pwr_in, shape="-|-", k=22)
        drawing.connect(cin.start, u5.VI, net=pwr_in, shape="|-")
        drawing.net_label(pwr_in, at=(42, 47))
        drawing.junction(pwr_in, at=(60, 52))

        drawing.connect(u5.VO, u3v3.VI, net=pwr_5v, shape="-")
        drawing.connect(c5v.start, u5.VO, net=pwr_5v, shape="|-")
        drawing.net_label(pwr_5v, at=(123, 47))
        drawing.junction(pwr_5v, at=(118, 52))

        drawing.connect(u3v3.VO, c3v3.start, net=pwr_3v3, shape="-|", k=10)
        drawing.off_page("+3V3", net=pwr_3v3, at=(214, 34), orient="Up")
        drawing.net_label(pwr_3v3, at=(204, 47))

        drawing.off_page("VDDA", net=pwr_vdda, at=cvdda.start, orient="Up")

        ground_bus = (34, 154)
        drawing.off_page("GND", net=pwr_gnd, at=ground_bus, orient="Down")
        for anchor in (
            pwr_j[2],
            pwr_j[3],
            pwr_j[4],
            u5.GND,
            u3v3.GND,
            cin.end,
            c5v.end,
            c3v3.end,
            cvdda.end,
        ):
            drawing.connect(anchor, ground_bus, net=pwr_gnd, shape="|-")
            drawing.junction(pwr_gnd, at=(anchor.x, 154))


def _author_usb_sheet(
    sheet: volt.Schematic,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
    net_by_pin: dict[int, volt.Net],
) -> None:
    usb = board.modules["USB"]

    with sheet.drawing(unit=20) as drawing:
        _rail_legend(drawing, nets, (24, 18), ("+5V", "+3V3", "GND"))

        usb_j = drawing.place(
            usb.component("J1"),
            at=(24, 44),
            symbol=lib.USB_B_MICRO.schematic_symbol,
        )
        usb_esd = drawing.place(
            usb.component("U1"),
            at=(98, 58),
            symbol=lib.USBLC6_4SC6.schematic_symbol,
        )
        swd = drawing.place(
            board.components["J2"],
            at=(218, 22),
            symbol=lib.JTAG_SWD_10.schematic_symbol,
        )
        gpio = drawing.place(
            board.components["J3"],
            at=(218, 126),
            symbol=lib.CONNECTOR_1X04.schematic_symbol,
        )

        drawing.connect(usb_j.VBUS, usb_esd.VBUS, net=nets["USB/VBUS"], shape="-|", k=46)
        drawing.net_label(nets["USB/VBUS"], at=(66, 38))
        drawing.connect(usb_j["D+"], usb_esd["I/O1"], net=nets["USB/USB_DP"], shape="-|", k=46)
        drawing.net_label(nets["USB/USB_DP"], at=(66, 56))
        drawing.wire(nets["USB/USB_DM"]).at(usb_j["D-"]).via((74, 54)).to(
            usb_esd["I/O2"]
        ).orthogonal()
        drawing.net_label(nets["USB/USB_DM"], at=(66, 50))

        _connect_to_off_page(drawing, net_by_pin, usb_esd["I/O4"], "MCU_USB_DP", label=True)
        _connect_to_off_page(drawing, net_by_pin, usb_esd["I/O3"], "MCU_USB_DM", label=True)

        usb_gnd = nets["USB/GND"]
        for anchor in (usb_j.GND, usb_j.Shield, usb_esd.GND):
            drawing.connect(anchor, (72, 124), net=usb_gnd, shape="|-")
            drawing.junction(usb_gnd, at=(anchor.x, 124))
        drawing.off_page("GND", net=usb_gnd, at=(72, 124), orient="Down")

        for anchor in (swd.VTref, gpio[1]):
            _rail(drawing, net_by_pin[anchor.pin.index], anchor, orient="Up")
        for anchor in (swd[3], swd[5], swd[9], gpio[4]):
            _rail(drawing, net_by_pin[anchor.pin.index], anchor, orient="Down")
        for name in ("SWDIO", "SWCLK", "SWO", "nRESET"):
            display = "NRST" if name == "nRESET" else name
            _connect_to_off_page(
                drawing,
                net_by_pin,
                swd[name],
                display,
                label=name in {"SWDIO", "SWCLK"},
            )
        _connect_to_off_page(drawing, net_by_pin, swd.TDI, "BOOT0")
        _connect_to_off_page(drawing, net_by_pin, gpio[2], "BOOT0")
        _mark_no_connects(drawing, (usb_j, swd, gpio), net_by_pin)


def _author_mcu_sheet(
    sheet: volt.Schematic,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
    net_by_pin: dict[int, volt.Net],
) -> None:
    support = board.modules["SUPPORT"]
    led = board.modules["LED_STATUS"]

    with sheet.drawing(unit=20) as drawing:
        _rail_legend(drawing, nets, (236, 18), ("+3V3", "VDDA", "GND"))

        stm32 = drawing.place(
            board.components["U1"],
            at=(102, 18),
            symbol=lib.STM32F405RGTx.schematic_symbol,
        )
        cvdd = drawing.C(support.component("CVDD"), symbol=TWO_TERMINAL_CAPACITOR).at(
            (24, 26)
        ).down()
        cvcap1 = drawing.C(support.component("CVCAP1"), symbol=TWO_TERMINAL_CAPACITOR).at(
            (24, 58)
        ).down()
        cvcap2 = drawing.C(support.component("CVCAP2"), symbol=TWO_TERMINAL_CAPACITOR).at(
            (24, 90)
        ).down()
        rreset = drawing.R(support.component("RRESET"), symbol=TWO_TERMINAL_RESISTOR).at(
            (24, 122)
        ).right()
        rboot = drawing.R(support.component("RBOOT"), symbol=TWO_TERMINAL_RESISTOR).at(
            (24, 150)
        ).right()
        swboot = drawing.place(
            support.component("SWBOOT"),
            at=(70, 140),
            symbol=lib.SPDT_SWITCH.schematic_symbol,
        )
        crystal = drawing.place(
            support.component("Y1"),
            at=(26, 174),
            symbol=lib.CRYSTAL_GND24.schematic_symbol,
        )
        chsein = drawing.C(support.component("CHSEIN"), symbol=TWO_TERMINAL_CAPACITOR).at(
            (74, 172)
        ).down()
        chseout = drawing.C(support.component("CHSEOUT"), symbol=TWO_TERMINAL_CAPACITOR).at(
            (74, 188)
        ).down()
        led_r = drawing.R(led.component("R"), symbol=TWO_TERMINAL_RESISTOR).at(
            (218, 184)
        ).right()
        led_d = drawing.LED(led.component("D"), symbol=_indicator_led_symbol()).at(
            (254, 184)
        ).right(1.2)

        for anchor in (*stm32.pins("VDD"), stm32.VBAT, stm32.VDDA):
            _rail(drawing, net_by_pin[anchor.pin.index], anchor, orient="Up")
        for anchor in (*stm32.pins("VSS"), stm32.VSSA):
            _rail(drawing, net_by_pin[anchor.pin.index], anchor, orient="Down")

        for name in (
            "MCU_USB_DM",
            "MCU_USB_DP",
            "SWDIO",
            "SWCLK",
            "SWO",
            "BOOT0",
            "NRST",
            "HSE_IN",
            "HSE_OUT",
            "VCAP_1",
            "VCAP_2",
            "STATUS_LED",
        ):
            pin_name = {
                "MCU_USB_DM": "PA11",
                "MCU_USB_DP": "PA12",
                "SWDIO": "PA13",
                "SWCLK": "PA14",
                "SWO": "PB3",
                "HSE_IN": "PH0",
                "HSE_OUT": "PH1",
                "STATUS_LED": "PC13",
            }.get(name, name)
            _connect_to_off_page(
                drawing,
                net_by_pin,
                stm32[pin_name],
                name,
                label=name in {"MCU_USB_DP", "MCU_USB_DM"},
            )

        support_vdd = nets["SUPPORT/VDD"]
        support_gnd = nets["SUPPORT/GND"]
        support_reset = nets["SUPPORT/NRST"]
        support_boot = nets["SUPPORT/BOOT0"]
        support_hse_in = nets["SUPPORT/HSE_IN"]
        support_hse_out = nets["SUPPORT/HSE_OUT"]

        drawing.off_page("+3V3", net=support_vdd, at=(64, 22), orient="Up")
        for anchor in (cvdd.start, rreset.start, swboot.A):
            drawing.connect(anchor, (64, 22), net=support_vdd, shape="-|")
            drawing.junction(support_vdd, at=(64, anchor.y))

        for anchor in (
            cvdd.end,
            cvcap1.end,
            cvcap2.end,
            rboot.end,
            swboot.B,
            crystal[2],
            crystal[4],
            chsein.end,
            chseout.end,
        ):
            drawing.off_page("GND", net=support_gnd, at=anchor, orient="Down")

        drawing.off_page("NRST", net=support_reset, at=rreset.end, orient="Right")
        drawing.connect(rboot.start, swboot.C, net=support_boot, shape="-")
        drawing.net_label(support_boot, at=(62, 145))
        drawing.off_page("BOOT0", net=support_boot, at=(92, 150), orient="Right")

        for anchor in (crystal[1], chsein.start):
            drawing.off_page("HSE_IN", net=support_hse_in, at=anchor, orient="Right")
        for anchor in (crystal[3], chseout.start):
            drawing.off_page("HSE_OUT", net=support_hse_out, at=anchor, orient="Right")
        drawing.off_page("VCAP_1", net=nets["SUPPORT/VCAP_1"], at=cvcap1.start, orient="Up")
        drawing.off_page("VCAP_2", net=nets["SUPPORT/VCAP_2"], at=cvcap2.start, orient="Up")

        led_supply = nets["LED_STATUS/SUPPLY"]
        led_signal = nets["LED_STATUS/SIGNAL"]
        led_gnd = nets["LED_STATUS/GND"]
        led_supply_port = drawing.off_page(
            "+3V3", net=led_supply, at=led_r.start.up(16), orient="Up"
        )
        drawing.connect(led_r.start, led_supply_port, net=led_supply, shape="-")
        drawing.connect(led_r.end, led_d.start, net=led_signal, shape="-")
        drawing.net_label(led_signal, at=(238, 178))
        drawing.off_page("STATUS_LED", net=led_signal, at=(246, 170), orient="Up")
        led_ground = drawing.off_page("GND", net=led_gnd, at=led_d.end.down(16), orient="Down")
        drawing.connect(led_d.end, led_ground, net=led_gnd, shape="-")
        _mark_no_connects(drawing, (stm32,), net_by_pin)


def _rail_legend(
    drawing: volt.SchematicDrawing,
    nets: dict[str, volt.Net],
    at: tuple[float, float],
    names: tuple[str, ...],
) -> None:
    x, y = at
    for index, name in enumerate(names):
        point = (x + index * 14, y)
        if name == "GND":
            drawing.ground(net=nets[name], at=point, orient="Down")
        else:
            drawing.power(name, net=nets[name], at=point, orient="Up")


def _rail(
    drawing: volt.SchematicDrawing,
    net: volt.Net,
    at: volt.SchematicPinAnchor,
    *,
    orient: str = "Up",
) -> None:
    name = _display_net_name(net.name)
    if net.name == "GND":
        drawing.ground(net=net, at=at, orient="Down")
    elif name == net.name and (name.startswith("+") or name == "VDDA"):
        drawing.power(name, net=net, at=at, orient=orient)
    else:
        drawing.off_page(name, net=net, at=at, orient=orient)


def _connect_to_off_page(
    drawing: volt.SchematicDrawing,
    net_by_pin: dict[int, volt.Net],
    anchor: volt.SchematicPinAnchor,
    name: str,
    *,
    distance: float = 24,
    label: bool = False,
) -> None:
    net = net_by_pin[anchor.pin.index]
    if anchor.orientation == "Left":
        port = drawing.off_page(name, net=net, at=anchor.left(distance), orient="Left")
    elif anchor.orientation == "Right":
        port = drawing.off_page(name, net=net, at=anchor.right(distance), orient="Right")
    elif anchor.orientation == "Up":
        port = drawing.off_page(name, net=net, at=anchor.up(distance), orient="Up")
    else:
        port = drawing.off_page(name, net=net, at=anchor.down(distance), orient="Down")
    drawing.connect(anchor, port, net=net, shape="-")
    if label:
        drawing.net_label(net, at=anchor.right(6))


def _mark_no_connects(
    drawing: volt.SchematicDrawing,
    placements: tuple[volt.PlacedSchematicElement, ...],
    net_by_pin: dict[int, volt.Net],
) -> None:
    for placement in placements:
        for anchor in placement.pin_anchors():
            if anchor.pin.index not in net_by_pin:
                drawing.no_connect(anchor, reason="intentionally unused")


def _display_net_name(name: str) -> str:
    local_name = name.rsplit("/", 1)[-1]
    return {
        "IN_12V": "+12V",
        "OUT_5V": "+5V",
        "OUT_3V3": "+3V3",
        "VDD": "+3V3",
        "SUPPLY": "+3V3",
        "VBUS": "+5V",
        "GNDDetect": "GND",
        "SIGNAL": "STATUS_LED",
    }.get(local_name, local_name)
