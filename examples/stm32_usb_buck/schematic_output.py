"""Manual schematic projection for the STM32 USB buck benchmark."""

from __future__ import annotations

import json

import volt
from volt.libraries import stm32_usb_buck as lib

from .stm32_board import Stm32UsbBuckBoard


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
    """Create a deterministic, manually authored schematic projection."""

    nets = {net.name: net for net in board.design.nets()}
    net_by_pin = {
        pin.index: net
        for net in board.design.nets()
        for pin in net.pins()
    }
    covered_pins: set[int] = set()
    placements: list[tuple[volt.Schematic, volt.SchematicSymbol]] = []

    def place(
        sheet: volt.Schematic,
        component: volt.Component,
        *,
        at: tuple[float, float],
        symbol,
    ) -> volt.SchematicSymbol:
        placed = sheet.place(component, at=at, symbol=symbol)
        placements.append((sheet, placed))
        return placed

    pwr = board.modules["PWR"]
    usb = board.modules["USB"]
    support = board.modules["SUPPORT"]
    led = board.modules["LED_STATUS"]

    power = board.design.schematic("Power")
    mcu = board.design.schematic("MCU")
    connectors = board.design.schematic("USB and Connectors")

    vin = place(power, board.components["VIN_SRC"], at=(14, 24), symbol=_external_supply_symbol())
    pwr_j = place(
        power,
        pwr.component("J"),
        at=(16, 78),
        symbol=lib.CONNECTOR_1X04.schematic_symbol,
    )
    u5 = place(power, pwr.component("U5"), at=(76, 42), symbol=lib.AP1117_15.schematic_symbol)
    u3v3 = place(power, pwr.component("U3V3"), at=(154, 42), symbol=lib.AP1117_15.schematic_symbol)
    cin = place(power, pwr.component("CIN"), at=(60, 118), symbol=lib.CAPACITOR.schematic_symbol)
    c5v = place(power, pwr.component("C5V"), at=(118, 118), symbol=lib.CAPACITOR.schematic_symbol)
    c3v3 = place(power, pwr.component("C3V3"), at=(176, 118), symbol=lib.CAPACITOR.schematic_symbol)
    cvdda = place(
        power,
        pwr.component("CVDDA"),
        at=(234, 118),
        symbol=lib.CAPACITOR.schematic_symbol,
    )

    usb_j = place(
        connectors,
        usb.component("J1"),
        at=(24, 44),
        symbol=lib.USB_B_MICRO.schematic_symbol,
    )
    usb_esd = place(
        connectors,
        usb.component("U1"),
        at=(98, 58),
        symbol=lib.USBLC6_4SC6.schematic_symbol,
    )
    swd = place(
        connectors,
        board.components["J2"],
        at=(218, 22),
        symbol=lib.JTAG_SWD_10.schematic_symbol,
    )
    gpio = place(
        connectors,
        board.components["J3"],
        at=(218, 126),
        symbol=lib.CONNECTOR_1X04.schematic_symbol,
    )

    stm32 = place(
        mcu,
        board.components["U1"],
        at=(102, 18),
        symbol=lib.STM32F405RGTx.schematic_symbol,
    )
    cvdd = place(mcu, support.component("CVDD"), at=(24, 26), symbol=lib.CAPACITOR.schematic_symbol)
    cvcap1 = place(
        mcu,
        support.component("CVCAP1"),
        at=(24, 58),
        symbol=lib.CAPACITOR.schematic_symbol,
    )
    cvcap2 = place(
        mcu,
        support.component("CVCAP2"),
        at=(24, 90),
        symbol=lib.CAPACITOR.schematic_symbol,
    )
    rreset = place(
        mcu,
        support.component("RRESET"),
        at=(24, 122),
        symbol=lib.RESISTOR.schematic_symbol,
    )
    rboot = place(
        mcu,
        support.component("RBOOT"),
        at=(24, 150),
        symbol=lib.RESISTOR.schematic_symbol,
    )
    swboot = place(
        mcu,
        support.component("SWBOOT"),
        at=(70, 140),
        symbol=lib.SPDT_SWITCH.schematic_symbol,
    )
    crystal = place(
        mcu,
        support.component("Y1"),
        at=(26, 174),
        symbol=lib.CRYSTAL_GND24.schematic_symbol,
    )
    chsein = place(
        mcu,
        support.component("CHSEIN"),
        at=(74, 172),
        symbol=lib.CAPACITOR.schematic_symbol,
    )
    chseout = place(
        mcu,
        support.component("CHSEOUT"),
        at=(74, 188),
        symbol=lib.CAPACITOR.schematic_symbol,
    )
    led_r = place(mcu, led.component("R"), at=(218, 184), symbol=lib.RESISTOR.schematic_symbol)
    led_d = place(mcu, led.component("D"), at=(254, 184), symbol=_indicator_led_symbol())

    author = _SchematicAuthor(net_by_pin, covered_pins)
    _author_power_sheet(author, power, board, nets, vin, pwr_j, u5, u3v3, cin, c5v, c3v3, cvdda)
    _author_usb_sheet(author, connectors, nets, usb_j, usb_esd, swd, gpio)
    _author_mcu_sheet(
        author,
        mcu,
        nets,
        stm32,
        cvdd,
        cvcap1,
        cvcap2,
        rreset,
        rboot,
        swboot,
        crystal,
        chsein,
        chseout,
        led_r,
        led_d,
    )
    _mark_no_connects(mcu, connectors, placements, net_by_pin)
    author.audit_no_fallback_pin_coverage(placements)
    return power


class _SchematicAuthor:
    def __init__(self, net_by_pin: dict[int, volt.Net], covered_pins: set[int]):
        self._net_by_pin = net_by_pin
        self._covered_pins = covered_pins

    def net(self, anchor: volt.SchematicPinAnchor) -> volt.Net:
        return self._net_by_pin[anchor.pin.index]

    def wire(
        self,
        sheet: volt.Schematic,
        net: volt.Net,
        *points,
        label_at: tuple[float, float] | volt.SchematicAnchor | None = None,
    ) -> None:
        route = sheet.wire(net).from_(points[0])
        for point in points[1:-1]:
            route.via(point)
        route.to(points[-1]).orthogonal()
        for point in points:
            self._cover(point)
        if label_at is not None:
            sheet.label(net, at=label_at)

    def off_page(
        self,
        sheet: volt.Schematic,
        name: str,
        *,
        net: volt.Net,
        at,
        orient: str = "Right",
    ) -> volt.SchematicPort:
        port = sheet.off_page(name, net=net, at=at, orient=orient)
        self._cover(at)
        return port

    def rail(self, sheet: volt.Schematic, net: volt.Net, at, *, orient: str = "Up") -> None:
        name = _display_net_name(net.name)
        if net.name == "GND":
            sheet.ground(net=net, at=at, orient="Down")
        elif name == net.name and (name.startswith("+") or name == "VDDA"):
            sheet.power(name, net=net, at=at, orient=orient)
        else:
            sheet.sheet_port(name, net=net, at=at, kind="OffPage", orient=orient)
        self._cover(at)

    def connect_to_off_page(
        self,
        sheet: volt.Schematic,
        anchor: volt.SchematicPinAnchor,
        name: str,
        *,
        distance: float = 24,
        label: bool = False,
    ) -> None:
        net = self.net(anchor)
        if anchor.orientation == "Left":
            port = self.off_page(sheet, name, net=net, at=anchor.left(distance), orient="Left")
        elif anchor.orientation == "Right":
            port = self.off_page(sheet, name, net=net, at=anchor.right(distance), orient="Right")
        elif anchor.orientation == "Up":
            port = self.off_page(sheet, name, net=net, at=anchor.up(distance), orient="Up")
        else:
            port = self.off_page(sheet, name, net=net, at=anchor.down(distance), orient="Down")
        label_at = anchor.right(6) if label else None
        self.wire(sheet, net, anchor, port.pin, label_at=label_at)

    def audit_no_fallback_pin_coverage(
        self,
        placements: list[tuple[volt.Schematic, volt.SchematicSymbol]],
    ) -> None:
        missing: list[str] = []
        for sheet, placement in placements:
            for anchor in placement.pin_anchors():
                if anchor.pin.index in self._covered_pins:
                    continue
                net = self._net_by_pin.get(anchor.pin.index)
                if net is None:
                    continue
                component_ref = self._component_reference(placement.component)
                missing.append(f"{sheet.name}: {component_ref}.{anchor.name} -> {net.name}")
        if missing:
            details = "\n".join(f"- {item}" for item in missing)
            raise RuntimeError(
                "No visible schematic coverage for connected pins after sugar authoring; "
                "fallback schematic pin coverage would hide unauthored connected pins:\n"
                f"{details}"
            )

    def _cover(self, point) -> None:
        if isinstance(point, volt.SchematicPinAnchor):
            self._covered_pins.add(point.pin.index)

    @staticmethod
    def _component_reference(component: volt.Component) -> str:
        logical = json.loads(component._design.to_json())
        component_id = f"component:{component.index}"
        for item in logical["components"]:
            if item["id"] == component_id:
                return item["reference"]
        return component_id


def _author_power_sheet(
    author: _SchematicAuthor,
    sheet: volt.Schematic,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
    vin: volt.SchematicSymbol,
    pwr_j: volt.SchematicSymbol,
    u5: volt.SchematicSymbol,
    u3v3: volt.SchematicSymbol,
    cin: volt.SchematicSymbol,
    c5v: volt.SchematicSymbol,
    c3v3: volt.SchematicSymbol,
    cvdda: volt.SchematicSymbol,
) -> None:
    _rail_legend(sheet, board.nets, (222, 18), ("+12V", "+5V", "+3V3", "VDDA", "GND"))

    author.rail(sheet, board.nets["+12V"], vin.pin("OUT"), orient="Right")
    author.rail(sheet, board.nets["GND"], vin.pin("GND"), orient="Down")

    pwr_in = nets["PWR/IN_12V"]
    pwr_5v = nets["PWR/OUT_5V"]
    pwr_3v3 = nets["PWR/OUT_3V3"]
    pwr_vdda = nets["PWR/VDDA"]
    pwr_gnd = nets["PWR/GND"]

    in_bus = (58, 52)
    author.wire(sheet, pwr_in, pwr_j.pin(1), (38, 78), (38, 52), in_bus, u5.pin("VI"))
    author.wire(sheet, pwr_in, cin.pin(1), cin.pin(1).up(66), in_bus, label_at=(42, 47))

    v5_bus = (136, 52)
    author.wire(sheet, pwr_5v, u5.pin("VO"), v5_bus, u3v3.pin("VI"), label_at=(123, 47))
    author.wire(sheet, pwr_5v, c5v.pin(1), c5v.pin(1).up(66), v5_bus)

    v3_bus = (214, 52)
    author.wire(sheet, pwr_3v3, u3v3.pin("VO"), v3_bus, c3v3.pin(1))
    author.off_page(sheet, "+3V3", net=pwr_3v3, at=(214, 34), orient="Up")
    sheet.label(pwr_3v3, at=(204, 47))

    author.off_page(sheet, "VDDA", net=pwr_vdda, at=cvdda.pin(1), orient="Up")

    ground_bus = (34, 154)
    author.off_page(sheet, "GND", net=pwr_gnd, at=ground_bus, orient="Down")
    for anchor in (
        pwr_j.pin(2),
        pwr_j.pin(3),
        pwr_j.pin(4),
        u5.pin("GND"),
        u3v3.pin("GND"),
        cin.pin(2),
        c5v.pin(2),
        c3v3.pin(2),
        cvdda.pin(2),
    ):
        tap = (anchor.x, 154)
        author.wire(sheet, pwr_gnd, anchor, tap, ground_bus)
        sheet.junction(pwr_gnd, at=tap)


def _author_usb_sheet(
    author: _SchematicAuthor,
    sheet: volt.Schematic,
    nets: dict[str, volt.Net],
    usb_j: volt.SchematicSymbol,
    usb_esd: volt.SchematicSymbol,
    swd: volt.SchematicSymbol,
    gpio: volt.SchematicSymbol,
) -> None:
    _rail_legend(sheet, nets, (24, 18), ("+5V", "+3V3", "GND"))

    author.wire(
        sheet,
        nets["USB/VBUS"],
        usb_j.pin("VBUS"),
        (70, 44),
        usb_esd.pin("VBUS"),
        label_at=(66, 38),
    )
    author.wire(
        sheet,
        nets["USB/USB_DP"],
        usb_j.pin("D+"),
        (70, 60),
        usb_esd.pin("I/O1"),
        label_at=(66, 56),
    )
    author.wire(
        sheet,
        nets["USB/USB_DM"],
        usb_j.pin("D-"),
        (74, 54),
        usb_esd.pin("I/O2"),
        label_at=(66, 50),
    )
    author.connect_to_off_page(sheet, usb_esd.pin("I/O4"), "MCU_USB_DP", label=True)
    author.connect_to_off_page(sheet, usb_esd.pin("I/O3"), "MCU_USB_DM", label=True)

    usb_gnd = nets["USB/GND"]
    for anchor in (usb_j.pin("GND"), usb_j.pin("Shield"), usb_esd.pin("GND")):
        author.wire(sheet, usb_gnd, anchor, (anchor.x, 124), (72, 124))
        sheet.junction(usb_gnd, at=(anchor.x, 124))
    author.off_page(sheet, "GND", net=usb_gnd, at=(72, 124), orient="Down")

    for anchor in (swd.pin("VTref"), gpio.pin(1)):
        author.rail(sheet, author.net(anchor), anchor, orient="Up")
    for anchor in (swd.pin(3), swd.pin(5), swd.pin(9), gpio.pin(4)):
        author.rail(sheet, author.net(anchor), anchor, orient="Down")
    for name in ("SWDIO", "SWCLK", "SWO", "nRESET"):
        display = "NRST" if name == "nRESET" else name
        author.connect_to_off_page(sheet, swd.pin(name), display, label=name in {"SWDIO", "SWCLK"})
    author.connect_to_off_page(sheet, swd.pin("TDI"), "BOOT0")
    author.connect_to_off_page(sheet, gpio.pin(2), "BOOT0")


def _author_mcu_sheet(
    author: _SchematicAuthor,
    sheet: volt.Schematic,
    nets: dict[str, volt.Net],
    stm32: volt.SchematicSymbol,
    cvdd: volt.SchematicSymbol,
    cvcap1: volt.SchematicSymbol,
    cvcap2: volt.SchematicSymbol,
    rreset: volt.SchematicSymbol,
    rboot: volt.SchematicSymbol,
    swboot: volt.SchematicSymbol,
    crystal: volt.SchematicSymbol,
    chsein: volt.SchematicSymbol,
    chseout: volt.SchematicSymbol,
    led_r: volt.SchematicSymbol,
    led_d: volt.SchematicSymbol,
) -> None:
    _rail_legend(sheet, nets, (236, 18), ("+3V3", "VDDA", "GND"))

    for anchor in (*stm32.pins("VDD"), stm32.pin("VBAT"), stm32.pin("VDDA")):
        author.rail(sheet, author.net(anchor), anchor, orient="Up")
    for anchor in (*stm32.pins("VSS"), stm32.pin("VSSA")):
        author.rail(sheet, author.net(anchor), anchor, orient="Down")

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
        author.connect_to_off_page(
            sheet,
            stm32.pin(pin_name),
            name,
            label=name in {"MCU_USB_DP", "MCU_USB_DM"},
        )

    support_vdd = nets["SUPPORT/VDD"]
    support_gnd = nets["SUPPORT/GND"]
    support_reset = nets["SUPPORT/NRST"]
    support_boot = nets["SUPPORT/BOOT0"]
    support_hse_in = nets["SUPPORT/HSE_IN"]
    support_hse_out = nets["SUPPORT/HSE_OUT"]

    author.off_page(sheet, "+3V3", net=support_vdd, at=(64, 22), orient="Up")
    for anchor in (cvdd.pin(1), rreset.pin(1), swboot.pin("A")):
        author.wire(sheet, support_vdd, anchor, (64, anchor.y), (64, 22))

    for anchor in (
        cvdd.pin(2),
        cvcap1.pin(2),
        cvcap2.pin(2),
        rboot.pin(2),
        swboot.pin("B"),
        crystal.pin(2),
        crystal.pin(4),
        chsein.pin(2),
        chseout.pin(2),
    ):
        author.off_page(sheet, "GND", net=support_gnd, at=anchor, orient="Down")

    author.off_page(sheet, "NRST", net=support_reset, at=rreset.pin(2), orient="Right")
    author.wire(sheet, support_boot, rboot.pin(1), (62, 150), swboot.pin("C"), label_at=(62, 145))
    author.off_page(sheet, "BOOT0", net=support_boot, at=(92, 150), orient="Right")
    author.wire(sheet, support_boot, swboot.pin("C"), (92, 150))

    for anchor in (crystal.pin(1), chsein.pin(1)):
        author.off_page(sheet, "HSE_IN", net=support_hse_in, at=anchor, orient="Right")
    for anchor in (crystal.pin(3), chseout.pin(1)):
        author.off_page(sheet, "HSE_OUT", net=support_hse_out, at=anchor, orient="Right")
    author.off_page(sheet, "VCAP_1", net=nets["SUPPORT/VCAP_1"], at=cvcap1.pin(1), orient="Up")
    author.off_page(sheet, "VCAP_2", net=nets["SUPPORT/VCAP_2"], at=cvcap2.pin(1), orient="Up")

    led_supply = nets["LED_STATUS/SUPPLY"]
    led_signal = nets["LED_STATUS/SIGNAL"]
    led_gnd = nets["LED_STATUS/GND"]
    author.off_page(sheet, "+3V3", net=led_supply, at=led_r.pin(1).up(16), orient="Up")
    author.wire(sheet, led_supply, led_r.pin(1), led_r.pin(1).up(16))
    author.wire(sheet, led_signal, led_r.pin(2), led_d.pin("A"), label_at=(238, 178))
    author.off_page(sheet, "STATUS_LED", net=led_signal, at=(246, 170), orient="Up")
    author.off_page(sheet, "GND", net=led_gnd, at=led_d.pin("K").down(16), orient="Down")
    author.wire(sheet, led_gnd, led_d.pin("K"), led_d.pin("K").down(16))


def _rail_legend(
    sheet: volt.Schematic,
    nets: dict[str, volt.Net],
    at: tuple[float, float],
    names: tuple[str, ...],
) -> None:
    x, y = at
    for index, name in enumerate(names):
        point = (x + index * 14, y)
        if name == "GND":
            sheet.ground(net=nets[name], at=point, orient="Down")
        else:
            sheet.power(name, net=nets[name], at=point, orient="Up")


def _mark_no_connects(
    mcu_sheet: volt.Schematic,
    connector_sheet: volt.Schematic,
    placements: list[tuple[volt.Schematic, volt.SchematicSymbol]],
    net_by_pin: dict[int, volt.Net],
) -> None:
    for sheet, placement in placements:
        if sheet not in {mcu_sheet, connector_sheet}:
            continue
        for anchor in placement.pin_anchors():
            if anchor.pin.index in net_by_pin:
                continue
            sheet.no_connect(anchor, reason="intentionally unused")


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
