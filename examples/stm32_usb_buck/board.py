"""PCB board stage for the STM32 USB buck benchmark."""

from __future__ import annotations

from pathlib import Path

import volt

from .stm32_board import Stm32UsbBuckBoard


BOARD_NAME = "STM32 USB Buck PCB"
JLCPCB_PROFILE_PROJECT_PATH = "profiles/jlcpcb_4layer.voltcap.json"
JLCPCB_PROFILE_PATH = Path(__file__).resolve().parent / JLCPCB_PROFILE_PROJECT_PATH


def build_pcb(context: volt.BuildContext) -> volt.Board:
    model = context.resource("stm32_board", Stm32UsbBuckBoard)
    design = context.design()
    nets = {net.name: net for net in design.nets()}

    board = design.board(BOARD_NAME)
    board.set_capability_profile(volt.CapabilityProfile.from_file(JLCPCB_PROFILE_PATH))
    front = board.add_layer("F.Cu", role="copper", side="top")
    inner1 = board.add_layer("In1.Cu", role="copper", side="inner")
    inner2 = board.add_layer("In2.Cu", role="copper", side="inner")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
    board.set_layer_stack((front, inner1, inner2, back), thickness=1.6)
    board.set_design_rules(
        copper_clearance=0.127,
        min_track_width=0.20,
        min_via_drill=0.30,
        min_via_annular=0.70,
        board_outline_clearance=0.25,
    )
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(90.0, 58.0))

    pwr = model.modules["PWR"]
    usb = model.modules["USB"]
    support = model.modules["SUPPORT"]
    led = model.modules["LED_STATUS"]

    with board.layout(unit=1.0, grid=0.5) as layout:
        placed = {
            "VIN_SRC": layout.place(model.components["VIN_SRC"], at=(6.0, 30.0), orient="right"),
            "PWR/J": layout.place(pwr.component("J"), at=(6.0, 17.0), orient="right"),
            "PWR/U5": layout.place(pwr.component("U5"), at=(23.0, 17.0), orient="right"),
            "PWR/U3V3": layout.place(pwr.component("U3V3"), at=(38.0, 17.0), orient="right"),
            "PWR/CIN": layout.two_pad(pwr.component("CIN")).at((17.0, 23.0)).anchor("center").right(),
            "PWR/C5V": layout.two_pad(pwr.component("C5V")).at((31.0, 23.0)).anchor("center").right(),
            "PWR/C3V3": layout.two_pad(pwr.component("C3V3")).at((46.0, 23.0)).anchor("center").right(),
            "PWR/CVDDA": layout.two_pad(pwr.component("CVDDA")).at((33.0, 30.0)).anchor("center").down(),
            "USB/J1": layout.place(usb.component("J1"), at=(84.0, 39.0), orient="left", locked=True),
            "USB/U1": layout.place(usb.component("U1"), at=(73.0, 37.5), orient="right"),
            "SUPPORT/CVDD": layout.two_pad(support.component("CVDD")).at((38.0, 22.0)).anchor("center").right(),
            "SUPPORT/CVCAP1": layout.two_pad(support.component("CVCAP1")).at((42.0, 43.0)).anchor("center").down(),
            "SUPPORT/CVCAP2": layout.two_pad(support.component("CVCAP2")).at((54.0, 43.0)).anchor("center").down(),
            "SUPPORT/RRESET": layout.two_pad(support.component("RRESET")).at((36.5, 27.0)).anchor("center").right(),
            "SUPPORT/RBOOT": layout.two_pad(support.component("RBOOT")).at((60.0, 25.0)).anchor("center").down(),
            "SUPPORT/SWBOOT": layout.place(support.component("SWBOOT"), at=(66.5, 24.5), orient="right"),
            "SUPPORT/Y1": layout.place(support.component("Y1"), at=(37.5, 33.0), orient="right"),
            "SUPPORT/CHSEIN": layout.two_pad(support.component("CHSEIN")).at((33.0, 36.0)).anchor("center").right(),
            "SUPPORT/CHSEOUT": layout.two_pad(support.component("CHSEOUT")).at((41.0, 39.0)).anchor("center").right(),
            "LED_STATUS/R": layout.two_pad(led.component("R")).at((62.0, 50.0)).anchor("center").right(),
            "LED_STATUS/D": layout.two_pad(led.component("D")).at((70.0, 50.0)).anchor("center").right(),
            "U1": layout.place(model.components["U1"], at=(49.0, 31.0), orient="right"),
            "J2": layout.place(model.components["J2"], at=(78.0, 17.0), orient="right", locked=True),
            "J3": layout.place(model.components["J3"], at=(84.0, 30.0), orient="left", locked=True),
            "H1": layout.place(model.components["H1"], at=(5.0, 5.0), orient="right", locked=True),
            "H2": layout.place(model.components["H2"], at=(85.0, 5.0), orient="right", locked=True),
            "H3": layout.place(model.components["H3"], at=(5.0, 53.0), orient="right", locked=True),
            "H4": layout.place(model.components["H4"], at=(85.0, 53.0), orient="right", locked=True),
        }

        _add_silkscreen(layout, silk)
        _route_power(model, nets, placed, layout, front, back)
        _route_usb(nets, placed, layout, front, back)
        _route_mcu_support(nets, placed, layout, front, inner2, back)
        _route_connectors(nets, placed, layout, front, inner1)
        _route_status_led(nets, placed, layout, front)
        layout.zone(
            layers=(back,),
            net=nets["GND"],
            at=(1.0, 49.0),
            size=(13.0, 8.0),
            fill="solid",
        )

    return board


def _add_silkscreen(layout, silk: int) -> None:
    labels = (
        ("STM32 USB BUCK", (30.0, 54.5), 0.0, 1.2),
        ("12V IN", (2.5, 17.0), 90.0, 0.8),
        ("USB", (81.0, 45.5), 0.0, 0.8),
        ("SWD", (71.0, 9.5), 0.0, 0.8),
        ("GPIO", (80.0, 29.0), 0.0, 0.8),
        ("+3V3", (47.0, 19.0), 0.0, 0.7),
        ("VDDA", (32.0, 25.5), 0.0, 0.7),
        ("BOOT0", (61.0, 20.5), 0.0, 0.7),
        ("RESET", (25.0, 29.5), 0.0, 0.7),
        ("STATUS", (61.0, 53.0), 0.0, 0.7),
        ("HSE 8MHZ", (19.5, 40.0), 0.0, 0.7),
        ("GND POUR B.CU", (12.0, 54.5), 0.0, 0.7),
    )
    for text, at, rotation, size in labels:
        layout.text(text, at=at, layer=silk, rotation=rotation, size=size)


def _chain(layout, net: volt.Net, anchors, *, layer: int, width: float) -> None:
    points = tuple(anchors)
    for start, end in zip(points, points[1:]):
        _connect(layout, net, start, end, layer=layer, width=width)


def _via_drop(layout, net: volt.Net, anchor, drop, *, front: int, back: int, width: float) -> None:
    _connect(layout, net, anchor, drop, layer=front, width=width)
    layout.via(net, at=drop, start_layer=front, end_layer=back)


def _connect(layout, net: volt.Net, start, end, *, layer: int, width: float) -> None:
    layout._board.assisted_connect(
        net,
        start=start.point,
        start_layer=layer,
        end=end.point,
        end_layer=layer,
    )


def _manual_trace(layout, net: volt.Net, anchors, *, layer: int, width: float) -> None:
    points = []
    for anchor in anchors:
        point = anchor.point if hasattr(anchor, "point") else tuple(anchor)
        if points and points[-1] == point:
            continue
        points.append(point)
    if len(points) < 2:
        return
    layout._board.add_track(net, layer=layer, points=tuple(points), width=width)


def _route_via_channel(
    layout,
    net: volt.Net,
    start,
    start_via,
    channel,
    end_via,
    end,
    *,
    front: int,
    back: int,
    width: float,
) -> None:
    _manual_trace(layout, net, (start, start_via), layer=front, width=width)
    layout._board.add_via(net, at=_board_point(start_via), start_layer=front, end_layer=back)
    _manual_trace(layout, net, (start_via, *channel, end_via), layer=back, width=width)
    layout._board.add_via(net, at=_board_point(end_via), start_layer=back, end_layer=front)
    _manual_trace(layout, net, (end_via, end), layer=front, width=width)


def _board_point(anchor) -> tuple[float, float]:
    return anchor.point if hasattr(anchor, "point") else tuple(anchor)


def _route_power(
    model: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
    placed: dict[str, object],
    layout,
    front: int,
    back: int,
) -> None:
    pwr_j = placed["PWR/J"]
    u5 = placed["PWR/U5"]
    u3v3 = placed["PWR/U3V3"]
    cin = placed["PWR/CIN"]
    c5v = placed["PWR/C5V"]
    c3v3 = placed["PWR/C3V3"]
    cvdda = placed["PWR/CVDDA"]
    vin = placed["VIN_SRC"]
    mcu = placed["U1"]
    swd = placed["J2"]
    gpio = placed["J3"]

    _connect(layout, nets["+12V"], vin["OUT"], vin["OUT"].right(3.5), layer=front, width=0.45)
    _chain(
        layout,
        nets["PWR/IN_12V"],
        (pwr_j[1], cin.start, u5["VI"]),
        layer=front,
        width=0.50,
    )
    _chain(
        layout,
        nets["PWR/OUT_5V"],
        (u5.pad("4"), u5.pad("2"), c5v.start, u3v3["VI"]),
        layer=front,
        width=0.45,
    )
    _chain(
        layout,
        nets["PWR/OUT_3V3"],
        (u3v3.pad("4"), u3v3.pad("2"), c3v3.start),
        layer=front,
        width=0.40,
    )
    _connect(layout, nets["PWR/VDDA"], cvdda.start, cvdda.start.down(3.0), layer=front, width=0.25)

    pwr_gnd_anchors = (
        pwr_j[2],
        pwr_j[3],
        pwr_j[4],
        cin.end,
        u5["GND"],
        c5v.end,
        u3v3["GND"],
        c3v3.end,
        cvdda.end,
    )
    pwr_gnd_drops = tuple(
        layout.node(anchor, dx=0.0, dy=2.0)
        for anchor in pwr_gnd_anchors
    )
    for anchor, drop in zip(pwr_gnd_anchors, pwr_gnd_drops):
        _via_drop(layout, nets["PWR/GND"], anchor, drop, front=front, back=back, width=0.30)
    _chain(layout, nets["PWR/GND"], pwr_gnd_drops, layer=back, width=0.40)

    vdd_anchors = (
        mcu["VBAT"],
        *mcu.pins("VDD"),
        swd["VTref"],
        gpio[1],
    )
    vdd_drops = tuple(_vdd_drop(layout, anchor) for anchor in vdd_anchors)
    for anchor, drop in zip(vdd_anchors, vdd_drops):
        _via_drop(layout, nets["+3V3"], anchor, drop, front=front, back=back, width=0.25)
    _chain(layout, nets["+3V3"], vdd_drops, layer=back, width=0.35)

    _connect(layout, nets["VDDA"], mcu["VDDA"], mcu["VDDA"].left(3.0), layer=front, width=0.25)

    gnd_placed = (
        placed["VIN_SRC"]["GND"],
        *mcu.pins("VSS"),
        mcu["VSSA"],
        swd[3],
        swd[5],
        swd["GNDDetect"],
        gpio[4],
        placed["H1"][1],
        placed["H2"][1],
        placed["H3"][1],
        placed["H4"][1],
    )
    gnd_drops = tuple(_gnd_drop(layout, anchor) for anchor in gnd_placed)
    for anchor, drop in zip(gnd_placed, gnd_drops):
        _via_drop(layout, nets["GND"], anchor, drop, front=front, back=back, width=0.30)
    _chain(layout, nets["GND"], gnd_drops, layer=back, width=0.45)


def _vdd_drop(layout, anchor):
    if anchor.x > 80.0:
        return layout.node(anchor, dx=-4.0)
    if anchor.x > 70.0:
        return layout.node(anchor, dy=-3.0)
    if anchor.y > 31.0:
        return layout.node(anchor, dy=3.0)
    if anchor.y < 25.0:
        return layout.node(anchor, dy=-3.0)
    return layout.node(anchor, dx=(2.0 if anchor.x < 49.0 else -2.0))


def _gnd_drop(layout, anchor):
    if anchor.x < 49.0:
        return layout.node(
            anchor,
            dx=1.25,
            dy=(3.0 if anchor.y < 31.0 else -4.0),
        )
    return layout.node(
        anchor,
        dx=-2.0,
        dy=(3.0 if anchor.y < 31.0 else -4.0),
    )


def _route_usb(
    nets: dict[str, volt.Net],
    placed: dict[str, object],
    layout,
    front: int,
    back: int,
) -> None:
    usb_j = placed["USB/J1"]
    esd = placed["USB/U1"]
    _connect(layout, nets["USB/VBUS"], usb_j["VBUS"], esd["VBUS"], layer=front, width=0.25)
    _connect(layout, nets["USB/USB_DP"], usb_j["D+"], esd["I/O1"], layer=front, width=0.20)
    d_minus_lane = usb_j["D-"].down(1.1)
    d_minus_escape = esd["I/O2"].left(1.35)
    _manual_trace(
        layout,
        nets["USB/USB_DM"],
        (
            usb_j["D-"],
            d_minus_lane,
            (d_minus_escape.x, d_minus_lane.y),
            d_minus_escape,
            esd["I/O2"],
        ),
        layer=front,
        width=0.20,
    )
    _connect(layout, nets["USB/MCU_USB_DP"], esd["I/O4"], esd["I/O4"].left(3.0), layer=front, width=0.20)
    _connect(layout, nets["USB/MCU_USB_DM"], esd["I/O3"], esd["I/O3"].left(3.0), layer=front, width=0.20)
    layout._board.add_via(nets["USB/GND"], at=usb_j["GND"].point, start_layer=front, end_layer=back)
    layout._board.add_via(nets["USB/GND"], at=usb_j["Shield"].point, start_layer=front, end_layer=back)
    layout._board.add_via(nets["USB/GND"], at=esd["GND"].point, start_layer=front, end_layer=back)
    _manual_trace(
        layout,
        nets["USB/GND"],
        (
            usb_j["Shield"],
            (76.0, usb_j["Shield"].y),
            (76.0, esd["GND"].y),
            esd["GND"],
        ),
        layer=back,
        width=0.30,
    )
    _manual_trace(
        layout,
        nets["USB/GND"],
        (
            usb_j["GND"],
            (82.7, 43.0),
            (76.0, 43.0),
            (76.0, esd["GND"].y),
        ),
        layer=back,
        width=0.30,
    )


def _route_mcu_support(
    nets: dict[str, volt.Net],
    placed: dict[str, object],
    layout,
    front: int,
    support_layer: int,
    back: int,
) -> None:
    cvdd = placed["SUPPORT/CVDD"]
    cvcap1 = placed["SUPPORT/CVCAP1"]
    cvcap2 = placed["SUPPORT/CVCAP2"]
    rreset = placed["SUPPORT/RRESET"]
    rboot = placed["SUPPORT/RBOOT"]
    swboot = placed["SUPPORT/SWBOOT"]
    crystal = placed["SUPPORT/Y1"]
    chsein = placed["SUPPORT/CHSEIN"]
    chseout = placed["SUPPORT/CHSEOUT"]

    _chain(layout, nets["SUPPORT/VDD"], (cvdd.start, rreset.start, swboot["A"]), layer=front, width=0.25)
    _chain(layout, nets["SUPPORT/BOOT0"], (rboot.start, swboot["C"]), layer=front, width=0.20)
    _chain(layout, nets["SUPPORT/HSE_IN"], (crystal[1], chsein.start), layer=front, width=0.20)
    _chain(layout, nets["SUPPORT/HSE_OUT"], (crystal[3], chseout.start), layer=front, width=0.20)
    _connect(layout, nets["SUPPORT/NRST"], rreset.end, rreset.end.right(3.0), layer=front, width=0.20)
    _connect(layout, nets["SUPPORT/VCAP_1"], cvcap1.start, cvcap1.start.up(2.5), layer=front, width=0.25)
    _connect(layout, nets["SUPPORT/VCAP_2"], cvcap2.start, cvcap2.start.up(2.5), layer=front, width=0.25)

    _connect(layout, nets["SUPPORT/GND"], cvdd.end, cvdd.end.left(2.5), layer=front, width=0.25)
    _route_support_ground(
        layout,
        nets["SUPPORT/GND"],
        (
            cvdd.end,
            crystal[2],
            crystal[4],
            chsein.end,
            chseout.end,
            cvcap1.end,
            cvcap2.end,
            rboot.end,
            swboot[3],
        ),
        front=front,
        support_layer=support_layer,
        through_layer=back,
    )


def _route_support_ground(
    layout,
    net: volt.Net,
    pads,
    *,
    front: int,
    support_layer: int,
    through_layer: int,
) -> None:
    for pad in pads:
        layout._board.add_via(net, at=pad.point, start_layer=front, end_layer=through_layer)

    cvdd, y1_gnd_a, y1_gnd_b, chsein, chseout, cvcap1, cvcap2, rboot, swboot_gnd = pads
    _manual_trace(
        layout,
        net,
        (
            cvdd,
            (31.0, cvdd.y),
            (31.0, cvcap1.y),
            cvcap1,
            cvcap2,
            (60.0, cvcap2.y),
            (60.0, rboot.y),
            rboot,
            (swboot_gnd.x, rboot.y),
            swboot_gnd,
        ),
        layer=support_layer,
        width=0.30,
    )
    for pad in (y1_gnd_a, y1_gnd_b, chsein, chseout):
        _manual_trace(
            layout,
            net,
            (pad, (31.0, pad.y)),
            layer=support_layer,
            width=0.25,
        )


def _route_connectors(
    nets: dict[str, volt.Net],
    placed: dict[str, object],
    layout,
    front: int,
    signal_layer: int,
) -> None:
    mcu = placed["U1"]
    swd = placed["J2"]
    gpio = placed["J3"]

    _connect(layout, nets["MCU_USB_DP"], mcu["PA12"], mcu["PA12"].right(7.0), layer=front, width=0.20)
    _connect(layout, nets["MCU_USB_DM"], mcu["PA11"], mcu["PA11"].right(7.0), layer=front, width=0.20)

    _route_via_channel(
        layout,
        nets["SWDIO"],
        mcu["PA13"],
        (62.5, mcu["PA13"].y),
        ((62.5, 35.5), (85.6, 35.5)),
        (85.6, swd["SWDIO"].y),
        swd["SWDIO"],
        front=front,
        back=signal_layer,
        width=0.20,
    )
    _route_via_channel(
        layout,
        nets["SWCLK"],
        mcu["PA14"],
        (52.75, 39.5),
        ((86.5, 39.5),),
        (86.5, swd["SWCLK"].y),
        swd["SWCLK"],
        front=front,
        back=signal_layer,
        width=0.20,
    )
    _route_via_channel(
        layout,
        nets["SWO"],
        mcu["PB3"],
        (49.75, 42.0),
        ((87.4, 42.0),),
        (87.4, swd["SWO"].y),
        swd["SWO"],
        front=front,
        back=signal_layer,
        width=0.20,
    )
    _route_via_channel(
        layout,
        nets["BOOT0"],
        mcu["BOOT0"],
        (47.25, 45.0),
        ((88.3, 45.0),),
        (88.3, swd["TDI"].y),
        swd["TDI"],
        front=front,
        back=signal_layer,
        width=0.20,
    )
    _route_via_channel(
        layout,
        nets["NRST"],
        mcu["NRST"],
        (41.0, 31.75),
        ((41.0, 47.0), (89.2, 47.0)),
        (89.2, swd["nRESET"].y),
        swd["nRESET"],
        front=front,
        back=signal_layer,
        width=0.20,
    )
    _connect(layout, nets["BOOT0"], mcu["BOOT0"], gpio[2], layer=front, width=0.20)


def _route_status_led(
    nets: dict[str, volt.Net],
    placed: dict[str, object],
    layout,
    front: int,
) -> None:
    resistor = placed["LED_STATUS/R"]
    led = placed["LED_STATUS/D"]
    mcu = placed["U1"]
    _connect(layout, nets["LED_STATUS/SUPPLY"], resistor.start, resistor.start.left(3.0), layer=front, width=0.25)
    _connect(layout, nets["LED_STATUS/SIGNAL"], resistor.end, led["A"], layer=front, width=0.20)
    _connect(layout, nets["LED_STATUS/GND"], led["K"], led["K"].right(2.5), layer=front, width=0.25)
    _connect(layout, nets["STATUS_LED"], mcu["PC13"], mcu["PC13"].left(3.0), layer=front, width=0.20)
