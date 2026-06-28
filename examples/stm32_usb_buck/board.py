"""PCB board stage for the STM32 USB buck benchmark."""

from __future__ import annotations

from pathlib import Path

import volt

from .stm32_board import Stm32UsbBuckBoard


BOARD_NAME = "STM32 USB Buck PCB"
JLCPCB_PROFILE_PROJECT_PATH = "profiles/jlcpcb_4layer.voltcap.json"
JLCPCB_PROFILE_PATH = Path(__file__).resolve().parent / JLCPCB_PROFILE_PROJECT_PATH
BOARD_SIZE = (90.0, 58.0)
BOARD_CORNER_RADIUS = 4.0
POWER_TRACE_MM = 0.20
SIGNAL_TRACE_MM = 0.15
VIA_DRILL_MM = 0.30
VIA_DIAMETER_MM = 0.50


def _rounded_rect_vertices(
    *,
    origin: tuple[float, float] = (0.0, 0.0),
    size: tuple[float, float] = BOARD_SIZE,
    radius: float = BOARD_CORNER_RADIUS,
) -> tuple[tuple[float, float], ...]:
    x, y = origin
    width, height = size
    offset = round(radius * 0.2929, 2)
    return (
        (x + radius, y),
        (x + width - radius, y),
        (x + width - offset, y + offset),
        (x + width, y + radius),
        (x + width, y + height - radius),
        (x + width - offset, y + height - offset),
        (x + width - radius, y + height),
        (x + radius, y + height),
        (x + offset, y + height - offset),
        (x, y + height - radius),
        (x, y + radius),
        (x + offset, y + offset),
    )


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
        copper_clearance=0.10,
        min_track_width=SIGNAL_TRACE_MM,
        min_via_drill=0.30,
        min_via_annular=0.50,
        board_outline_clearance=0.25,
    )
    board.set_polygon_outline(_rounded_rect_vertices())

    pwr = model.modules["PWR"]
    usb = model.modules["USB"]
    support = model.modules["SUPPORT"]
    led = model.modules["LED_STATUS"]

    with board.layout(unit=1.0, grid=0.5) as layout:
        placed = {
            "VIN_SRC": layout.place(model.components["VIN_SRC"], at=(6.0, 30.0), orient="right"),
            "PWR/J": layout.place(pwr.component("J"), at=(6.0, 17.0), orient="right"),
            "PWR/F1": layout.two_pad(pwr.component("F1")).at((13.5, 12.0)).anchor("center").right(),
            "PWR/FB1": layout.two_pad(pwr.component("FB1")).at((19.0, 12.0)).anchor("center").right(),
            "PWR/U5": layout.place(pwr.component("U5"), at=(27.5, 16.5), orient="right"),
            "PWR/L1": layout.two_pad(pwr.component("L1")).at((36.0, 16.5)).anchor("center").right(),
            "PWR/DSW": layout.two_pad(pwr.component("DSW")).at((27.5, 23.0)).anchor("center").right(),
            "PWR/CIN": layout.two_pad(pwr.component("CIN")).at((22.5, 18.0)).anchor("center").down(),
            "PWR/CBST": layout.two_pad(pwr.component("CBST")).at((31.5, 10.5)).anchor("center").right(),
            "PWR/C5V": layout.two_pad(pwr.component("C5V")).at((42.0, 17.0)).anchor("center").down(),
            "PWR/REN_TOP": layout.two_pad(pwr.component("REN_TOP")).at((23.5, 8.0)).anchor("center").right(),
            "PWR/REN_BOT": layout.two_pad(pwr.component("REN_BOT")).at((23.5, 10.5)).anchor("center").right(),
            "PWR/RFB_TOP": layout.two_pad(pwr.component("RFB_TOP")).at((40.0, 10.5)).anchor("center").right(),
            "PWR/RFB_BOT": layout.two_pad(pwr.component("RFB_BOT")).at((40.0, 13.0)).anchor("center").right(),
            "PWR/U3V3": layout.place(pwr.component("U3V3"), at=(55.0, 13.5), orient="right"),
            "PWR/C3V3": layout.two_pad(pwr.component("C3V3")).at((63.0, 16.0)).anchor("center").down(),
            "PWR/FBVDDA": layout.two_pad(pwr.component("FBVDDA")).at((40.5, 28.5)).anchor("center").right(),
            "PWR/CVDDA": layout.two_pad(pwr.component("CVDDA")).at((37.5, 29.0)).anchor("center").down(),
            "USB/J1": layout.place(usb.component("J1"), at=(84.0, 39.0), orient="left", locked=True),
            "USB/U1": layout.place(usb.component("U1"), at=(73.0, 37.5), orient="right"),
            "SUPPORT/CVDD": layout.two_pad(support.component("CVDD")).at((39.0, 22.0)).anchor("center").right(),
            "SUPPORT/CVCAP1": layout.two_pad(support.component("CVCAP1")).at((42.0, 43.0)).anchor("center").down(),
            "SUPPORT/CVCAP2": layout.two_pad(support.component("CVCAP2")).at((54.0, 43.0)).anchor("center").down(),
            "SUPPORT/RRESET": layout.two_pad(support.component("RRESET")).at((36.0, 26.5)).anchor("center").right(),
            "SUPPORT/RBOOT": layout.two_pad(support.component("RBOOT")).at((60.0, 25.0)).anchor("center").down(),
            "SUPPORT/SWBOOT": layout.place(support.component("SWBOOT"), at=(66.5, 24.5), orient="right"),
            "SUPPORT/Y1": layout.place(support.component("Y1"), at=(36.5, 34.0), orient="right"),
            "SUPPORT/CHSEIN": layout.two_pad(support.component("CHSEIN")).at((32.5, 36.5)).anchor("center").right(),
            "SUPPORT/CHSEOUT": layout.two_pad(support.component("CHSEOUT")).at((40.5, 39.5)).anchor("center").right(),
            "LED_STATUS/R": layout.two_pad(led.component("R")).at((62.0, 50.0)).anchor("center").right(),
            "LED_STATUS/D": layout.two_pad(led.component("D")).at((70.0, 50.0)).anchor("center").right(),
            "U1": layout.place(model.components["U1"], at=(49.0, 31.0), orient="right"),
            "J2": layout.place(model.components["J2"], at=(78.0, 17.0), orient="right", locked=True),
            "J3": layout.place(model.components["J3"], at=(84.0, 30.0), orient="left", locked=True),
            "H1": layout.place(model.components["H1"], at=(6.0, 6.0), orient="right", locked=True),
            "H2": layout.place(model.components["H2"], at=(84.0, 6.0), orient="right", locked=True),
            "H3": layout.place(model.components["H3"], at=(6.0, 52.0), orient="right", locked=True),
            "H4": layout.place(model.components["H4"], at=(84.0, 52.0), orient="right", locked=True),
        }

        _add_silkscreen(layout, silk)
        _route_power(model, nets, placed, layout, front, inner1, inner2, back)
        _route_usb(nets, placed, layout, front, back)
        _route_mcu_support(nets, placed, layout, front, inner1, back)
        _route_connectors(nets, placed, layout, front, inner1)
        _route_status_led(nets, placed, layout, front)
        _route_module_boundaries(nets, placed, layout, front, inner1, inner2, back)
        layout.zone(
            layers=(back,),
            net=nets["GND"],
            at=(4.5, 48.0),
            size=(16.0, 6.0),
            fill="solid",
        )

    return board


def _add_silkscreen(layout, silk: int) -> None:
    labels = (
        ("STM32 USB BUCK", (30.0, 55.0), 0.0, 1.1),
        ("12V IN", (2.5, 17.0), 90.0, 0.75),
        ("BUCK 5V", (31.0, 6.0), 0.0, 0.7),
        ("USB", (81.0, 45.5), 0.0, 0.8),
        ("SWD", (71.0, 9.5), 0.0, 0.8),
        ("GPIO", (80.0, 29.0), 0.0, 0.8),
        ("+3V3", (62.0, 19.0), 0.0, 0.7),
        ("VDDA", (34.0, 29.5), 0.0, 0.7),
        ("BOOT0", (72.0, 24.0), 0.0, 0.7),
        ("RESET", (27.0, 28.5), 0.0, 0.7),
        ("STATUS", (61.0, 53.0), 0.0, 0.7),
        ("HSE 8MHZ", (19.5, 40.0), 0.0, 0.7),
        ("GND POUR B.CU", (12.0, 55.0), 0.0, 0.65),
    )
    for text, at, rotation, size in labels:
        layout.text(text, at=at, layer=silk, rotation=rotation, size=size)


def _chain(layout, net: volt.Net, anchors, *, layer: int, width: float) -> None:
    points = tuple(anchors)
    for start, end in zip(points, points[1:]):
        _connect(layout, net, start, end, layer=layer, width=width)


def _via_drop(
    layout,
    net: volt.Net,
    anchor,
    drop,
    *,
    front: int,
    back: int,
    width: float,
    direct: bool = False,
) -> None:
    if direct:
        _manual_trace(layout, net, (anchor, drop), layer=front, width=width)
    else:
        _connect(layout, net, anchor, drop, layer=front, width=width)
    _via(layout, net, at=drop, start_layer=front, end_layer=back)


def _connect(layout, net: volt.Net, start, end, *, layer: int, width: float) -> None:
    layout._board.assisted_connect(
        net,
        start=start.point,
        start_layer=layer,
        end=end.point,
        end_layer=layer,
        width=width,
    )


def _via(layout, net: volt.Net, *, at, start_layer: int, end_layer: int) -> None:
    layout._board.add_via(
        net,
        at=_board_point(at),
        start_layer=start_layer,
        end_layer=end_layer,
        drill=VIA_DRILL_MM,
        annular=VIA_DIAMETER_MM,
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


def _orthogonal_chain(layout, net: volt.Net, anchors, *, layer: int, width: float) -> None:
    points = tuple(_board_point(anchor) for anchor in anchors)
    for start, end in zip(points, points[1:]):
        if start[0] == end[0] or start[1] == end[1]:
            _manual_trace(layout, net, (start, end), layer=layer, width=width)
            continue
        _manual_trace(
            layout,
            net,
            (start, (end[0], start[1]), end),
            layer=layer,
            width=width,
        )


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
    _via(layout, net, at=start_via, start_layer=front, end_layer=back)
    _manual_trace(layout, net, (start_via, *channel, end_via), layer=back, width=width)
    _via(layout, net, at=end_via, start_layer=back, end_layer=front)
    _manual_trace(layout, net, (end_via, end), layer=front, width=width)


def _board_point(anchor) -> tuple[float, float]:
    return anchor.point if hasattr(anchor, "point") else tuple(anchor)


def _route_power(
    model: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
    placed: dict[str, object],
    layout,
    front: int,
    control_layer: int,
    bootstrap_layer: int,
    back: int,
) -> None:
    pwr_j = placed["PWR/J"]
    f1 = placed["PWR/F1"]
    fb1 = placed["PWR/FB1"]
    u5 = placed["PWR/U5"]
    l1 = placed["PWR/L1"]
    dsw = placed["PWR/DSW"]
    cin = placed["PWR/CIN"]
    cboot = placed["PWR/CBST"]
    c5v = placed["PWR/C5V"]
    ren_top = placed["PWR/REN_TOP"]
    ren_bottom = placed["PWR/REN_BOT"]
    rfb_top = placed["PWR/RFB_TOP"]
    rfb_bottom = placed["PWR/RFB_BOT"]
    u3v3 = placed["PWR/U3V3"]
    c3v3 = placed["PWR/C3V3"]
    fbvdda = placed["PWR/FBVDDA"]
    cvdda = placed["PWR/CVDDA"]
    vin = placed["VIN_SRC"]
    mcu = placed["U1"]
    swd = placed["J2"]
    gpio = placed["J3"]

    _connect(layout, nets["+12V"], vin["OUT"], vin["OUT"].right(3.5), layer=front, width=0.20)
    _chain(
        layout,
        nets["PWR/IN_12V"],
        (pwr_j[1], f1.start),
        layer=front,
        width=0.20,
    )
    _chain(
        layout,
        nets["PWR/FUSED_12V"],
        (f1.end, fb1.start),
        layer=front,
        width=0.20,
    )
    _chain(
        layout,
        nets["PWR/BUCK_IN"],
        (fb1.end, cin.start, u5["IN"], ren_top.start),
        layer=front,
        width=0.20,
    )
    _chain(
        layout,
        nets["PWR/BUCK_EN"],
        (ren_top.end, ren_bottom.start, u5["EN"]),
        layer=front,
        width=0.20,
    )
    _route_via_channel(
        layout,
        nets["PWR/BUCK_EN"],
        u5["EN"],
        (30.25, u5["EN"].y),
        ((30.25, ren_bottom.start.y),),
        (21.5, ren_bottom.start.y),
        ren_bottom.start,
        front=front,
        back=control_layer,
        width=0.20,
    )
    _chain(
        layout,
        nets["PWR/BUCK_SW"],
        (u5["SW"], l1.start, dsw["K"], cboot.start),
        layer=front,
        width=0.20,
    )
    _chain(
        layout,
        nets["PWR/BUCK_BST"],
        (u5["BST"], cboot.end),
        layer=front,
        width=0.20,
    )
    _route_via_channel(
        layout,
        nets["PWR/BUCK_BST"],
        u5["BST"],
        (24.0, u5["BST"].y),
        ((24.0, 8.75),),
        (cboot.end.x, 8.75),
        cboot.end,
        front=front,
        back=bootstrap_layer,
        width=0.20,
    )
    _chain(
        layout,
        nets["PWR/OUT_5V"],
        (l1.end, c5v.start, rfb_top.start, u3v3["VI"]),
        layer=front,
        width=0.20,
    )
    _chain(
        layout,
        nets["PWR/BUCK_FB"],
        (rfb_top.end, rfb_bottom.start, u5["FB"]),
        layer=front,
        width=0.20,
    )
    _route_via_channel(
        layout,
        nets["PWR/BUCK_FB"],
        u5["FB"],
        (26.5, u5["FB"].y),
        ((26.5, 19.0), (37.0, 19.0)),
        (37.0, rfb_bottom.start.y),
        rfb_bottom.start,
        front=front,
        back=control_layer,
        width=0.20,
    )
    _chain(
        layout,
        nets["PWR/OUT_3V3"],
        (u3v3.pad("4"), u3v3.pad("2"), c3v3.start, fbvdda.start),
        layer=front,
        width=0.20,
    )
    _route_via_channel(
        layout,
        nets["PWR/VDDA"],
        fbvdda.end,
        fbvdda.end.right(1.0),
        ((42.25, 27.0), (37.5, 27.0)),
        cvdda.start.up(1.25),
        cvdda.start,
        front=front,
        back=control_layer,
        width=0.20,
    )

    pwr_gnd_anchors = (
        pwr_j[2],
        pwr_j[3],
        pwr_j[4],
        cin.end,
        u5["GND"],
        ren_bottom.end,
        dsw["A"],
        c5v.end,
        rfb_bottom.end,
        u3v3["GND"],
        c3v3.end,
        cvdda.end,
    )
    pwr_gnd_drops = (
        (pwr_j[2].x + 2.0, pwr_j[2].y),
        (pwr_j[3].x + 2.0, pwr_j[3].y),
        (pwr_j[4].x + 2.0, pwr_j[4].y),
        (cin.end.x - 2.0, cin.end.y),
        (u5["GND"].x - 1.5, u5["GND"].y),
        (ren_bottom.end.x + 2.0, ren_bottom.end.y + 2.0),
        (dsw["A"].x, dsw["A"].y + 3.0),
        (c5v.end.x, c5v.end.y + 3.0),
        (rfb_bottom.end.x + 2.25, rfb_bottom.end.y),
        (u3v3["GND"].x, u3v3["GND"].y - 2.0),
        (c3v3.end.x, c3v3.end.y + 3.0),
        (cvdda.end.x - 2.0, cvdda.end.y),
    )
    for anchor, drop in zip(pwr_gnd_anchors, pwr_gnd_drops):
        _via_drop(
            layout,
            nets["PWR/GND"],
            anchor,
            drop,
            front=front,
            back=back,
            width=0.20,
            direct=True,
        )
    _orthogonal_chain(layout, nets["PWR/GND"], pwr_gnd_drops, layer=back, width=0.20)

    vdd_anchors = (
        mcu["VBAT"],
        *mcu.pins("VDD"),
        swd["VTref"],
        gpio[1],
    )
    vdd_drops = tuple(_vdd_drop(layout, anchor) for anchor in vdd_anchors)
    for anchor, drop in zip(vdd_anchors, vdd_drops):
        _via_drop(layout, nets["+3V3"], anchor, drop, front=front, back=back, width=0.20)
    _chain(layout, nets["+3V3"], vdd_drops, layer=back, width=0.20)

    _connect(layout, nets["VDDA"], mcu["VDDA"], mcu["VDDA"].left(3.0), layer=front, width=0.20)

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
        _via_drop(layout, nets["GND"], anchor, drop, front=front, back=back, width=0.20)
    _chain(layout, nets["GND"], gnd_drops, layer=back, width=0.20)


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
    _connect(layout, nets["USB/VBUS"], usb_j["VBUS"], esd["VBUS"], layer=front, width=0.20)
    _connect(
        layout,
        nets["USB/USB_DP"],
        usb_j["D+"],
        esd["I/O1"],
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
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
        width=SIGNAL_TRACE_MM,
    )
    _connect(
        layout,
        nets["USB/MCU_USB_DP"],
        esd["I/O4"],
        esd["I/O4"].left(3.0),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _connect(
        layout,
        nets["USB/MCU_USB_DM"],
        esd["I/O3"],
        esd["I/O3"].left(3.0),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _via(layout, nets["USB/GND"], at=usb_j["GND"], start_layer=front, end_layer=back)
    _via(layout, nets["USB/GND"], at=usb_j["Shield"], start_layer=front, end_layer=back)
    _via(layout, nets["USB/GND"], at=esd["GND"], start_layer=front, end_layer=back)
    gnd_bus_x = esd["GND"].x - 2.0
    gnd_bus_y = usb_j["GND"].y + 1.2
    _manual_trace(
        layout,
        nets["USB/GND"],
        (
            usb_j["Shield"],
            (gnd_bus_x, usb_j["Shield"].y),
            (gnd_bus_x, esd["GND"].y),
            esd["GND"],
        ),
        layer=back,
        width=0.20,
    )
    _manual_trace(
        layout,
        nets["USB/GND"],
        (
            usb_j["GND"],
            (usb_j["GND"].x, gnd_bus_y),
            (gnd_bus_x, gnd_bus_y),
            (gnd_bus_x, esd["GND"].y),
        ),
        layer=back,
        width=0.20,
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

    _chain(
        layout,
        nets["SUPPORT/VDD"],
        (cvdd.start, rreset.start, swboot["A"]),
        layer=front,
        width=0.20,
    )
    _chain(
        layout,
        nets["SUPPORT/BOOT0"],
        (rboot.start, swboot["C"]),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _chain(
        layout,
        nets["SUPPORT/HSE_IN"],
        (crystal[1], chsein.start),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _chain(
        layout,
        nets["SUPPORT/HSE_OUT"],
        (crystal[3], chseout.start),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _connect(
        layout,
        nets["SUPPORT/NRST"],
        rreset.end,
        rreset.end.right(3.0),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _connect(
        layout,
        nets["SUPPORT/VCAP_1"],
        cvcap1.start,
        cvcap1.start.up(2.5),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _connect(
        layout,
        nets["SUPPORT/VCAP_2"],
        cvcap2.start,
        cvcap2.start.up(2.5),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )

    _connect(layout, nets["SUPPORT/GND"], cvdd.end, cvdd.end.left(2.5), layer=front, width=0.20)
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
        _via(layout, net, at=pad, start_layer=front, end_layer=through_layer)

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
            (swboot_gnd.x, cvcap2.y),
            swboot_gnd,
        ),
        layer=through_layer,
        width=0.20,
    )
    _manual_trace(
        layout,
        net,
        (
            (swboot_gnd.x, 30.5),
            (rboot.x, 30.5),
            rboot,
        ),
        layer=support_layer,
        width=0.20,
    )
    for pad in (y1_gnd_a, y1_gnd_b, chsein, chseout):
        _manual_trace(
            layout,
            net,
            (pad, (31.0, pad.y)),
            layer=through_layer,
            width=0.20,
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

    _connect(
        layout,
        nets["USB_DP"],
        mcu["PA12"],
        mcu["PA12"].right(7.0),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _connect(
        layout,
        nets["USB_DM"],
        mcu["PA11"],
        mcu["PA11"].right(7.0),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )

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
        width=SIGNAL_TRACE_MM,
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
        width=SIGNAL_TRACE_MM,
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
        width=SIGNAL_TRACE_MM,
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
        width=SIGNAL_TRACE_MM,
    )
    _route_via_channel(
        layout,
        nets["NRST"],
        mcu["NRST"],
        (39.0, 31.75),
        ((39.0, 47.0), (89.2, 47.0)),
        (89.2, swd["nRESET"].y),
        swd["nRESET"],
        front=front,
        back=signal_layer,
        width=SIGNAL_TRACE_MM,
    )
    gpio_boot_via = (88.3, gpio[2].y)
    _manual_trace(
        layout,
        nets["BOOT0"],
        (gpio[2], gpio_boot_via),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _via(layout, nets["BOOT0"], at=gpio_boot_via, start_layer=front, end_layer=signal_layer)


def _route_status_led(
    nets: dict[str, volt.Net],
    placed: dict[str, object],
    layout,
    front: int,
) -> None:
    resistor = placed["LED_STATUS/R"]
    led = placed["LED_STATUS/D"]
    mcu = placed["U1"]
    _manual_trace(
        layout,
        nets["LED_STATUS/SUPPLY"],
        (resistor.start, resistor.start.left(3.0)),
        layer=front,
        width=0.20,
    )
    _manual_trace(
        layout,
        nets["LED_STATUS/LED_A"],
        (resistor.end, led["A"]),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _manual_trace(
        layout,
        nets["LED_STATUS/SIGNAL"],
        (led["K"], led["K"].right(2.5)),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _connect(
        layout,
        nets["STATUS_LED"],
        mcu["PC13"],
        mcu["PC13"].left(1.0),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )


def _route_module_boundaries(
    nets: dict[str, volt.Net],
    placed: dict[str, object],
    layout,
    front: int,
    escape_layer: int,
    support_layer: int,
    back: int,
) -> None:
    pwr_j = placed["PWR/J"]
    u3v3 = placed["PWR/U3V3"]
    fbvdda = placed["PWR/FBVDDA"]
    cvdda = placed["PWR/CVDDA"]
    usb_j = placed["USB/J1"]
    usb = placed["USB/U1"]
    support_cvdd = placed["SUPPORT/CVDD"]
    support_reset = placed["SUPPORT/RRESET"]
    support_boot = placed["SUPPORT/SWBOOT"]
    crystal = placed["SUPPORT/Y1"]
    cvcap1 = placed["SUPPORT/CVCAP1"]
    cvcap2 = placed["SUPPORT/CVCAP2"]
    led_resistor = placed["LED_STATUS/R"]
    led = placed["LED_STATUS/D"]
    mcu = placed["U1"]
    vin = placed["VIN_SRC"]

    _manual_trace(
        layout,
        nets["PWR/IN_12V"],
        (vin["OUT"], (9.5, vin["OUT"].y), (9.5, pwr_j[1].y), pwr_j[1]),
        layer=front,
        width=POWER_TRACE_MM,
    )
    _manual_trace(
        layout,
        nets["PWR/GND"],
        (vin["GND"], (10.0, vin["GND"].y), (10.0, pwr_j[4].y), pwr_j[4]),
        layer=back,
        width=POWER_TRACE_MM,
    )

    pwr_usb_entry = u3v3["VI"].right(3.0)
    pwr_usb_exit = (76.0, 44.0)
    _manual_trace(
        layout,
        nets["PWR/OUT_5V"],
        (u3v3["VI"], pwr_usb_entry),
        layer=front,
        width=POWER_TRACE_MM,
    )
    _via(layout, nets["PWR/OUT_5V"], at=pwr_usb_entry, start_layer=front, end_layer=support_layer)
    _manual_trace(
        layout,
        nets["PWR/OUT_5V"],
        (pwr_usb_entry, (88.8, pwr_usb_entry.y), (88.8, pwr_usb_exit[1]), pwr_usb_exit),
        layer=support_layer,
        width=POWER_TRACE_MM,
    )
    _via(layout, nets["PWR/OUT_5V"], at=pwr_usb_exit, start_layer=support_layer, end_layer=front)
    _manual_trace(
        layout,
        nets["PWR/OUT_5V"],
        (pwr_usb_exit, (88.8, pwr_usb_exit[1]), (88.8, usb_j["VBUS"].y), usb_j["VBUS"]),
        layer=front,
        width=POWER_TRACE_MM,
    )
    _manual_trace(
        layout,
        nets["PWR/OUT_3V3"],
        ((49.71875, 23.734375), (49.71875, 25.0)),
        layer=front,
        width=POWER_TRACE_MM,
    )
    _via(layout, nets["PWR/OUT_3V3"], at=(49.71875, 25.0), start_layer=front, end_layer=back)
    _manual_trace(
        layout,
        nets["PWR/OUT_3V3"],
        ((49.71875, 25.0), (48.5, 25.0)),
        layer=back,
        width=POWER_TRACE_MM,
    )
    _manual_trace(
        layout,
        nets["PWR/VDDA"],
        (fbvdda.end, (42.95, fbvdda.end.y), mcu["VDDA"]),
        layer=front,
        width=POWER_TRACE_MM,
    )
    _manual_trace(
        layout,
        nets["PWR/GND"],
        ((35.5, 29.75), (35.5, 27.5)),
        layer=back,
        width=POWER_TRACE_MM,
    )

    _manual_trace(
        layout,
        nets["USB/GND"],
        (usb["GND"], (69.75, usb["GND"].y), (69.75, 34.6875)),
        layer=back,
        width=POWER_TRACE_MM,
    )
    _route_via_channel(
        layout,
        nets["USB/MCU_USB_DP"],
        usb["I/O4"],
        usb["I/O4"].left(1.2),
        ((66.0, usb["I/O4"].y), (66.0, 40.5), (56.6, 40.5)),
        (56.6, mcu["PA12"].y),
        mcu["PA12"],
        front=front,
        back=support_layer,
        width=SIGNAL_TRACE_MM,
    )
    _route_via_channel(
        layout,
        nets["USB/MCU_USB_DM"],
        usb["I/O3"],
        usb["I/O3"].left(1.2),
        ((64.0, usb["I/O3"].y), (64.0, 31.5), (55.8, 31.5)),
        mcu["PA11"].right(1.0),
        mcu["PA11"],
        front=front,
        back=support_layer,
        width=SIGNAL_TRACE_MM,
    )

    _manual_trace(
        layout,
        nets["SUPPORT/VDD"],
        ((49.825, 24.146875), (49.825, 25.2), (48.5, 25.2)),
        layer=front,
        width=0.20,
    )
    _manual_trace(
        layout,
        nets["SUPPORT/GND"],
        ((31.0, 22.0), (31.0, 27.5)),
        layer=back,
        width=0.20,
    )
    _route_via_channel(
        layout,
        nets["SUPPORT/NRST"],
        support_reset.end,
        (38.4, support_reset.end.y),
        ((38.4, 30.75), (41.2, 30.75)),
        mcu["NRST"].left(1.0),
        mcu["NRST"],
        front=front,
        back=support_layer,
        width=SIGNAL_TRACE_MM,
    )
    _manual_trace(
        layout,
        nets["SUPPORT/BOOT0"],
        (support_boot["C"], (66.5, 28.5), (48.75, 28.5), (48.75, mcu["BOOT0"].y), mcu["BOOT0"]),
        layer=front,
        width=SIGNAL_TRACE_MM,
    )
    _route_via_channel(
        layout,
        nets["SUPPORT/HSE_IN"],
        crystal[1],
        (34.5, 32.3),
        ((34.5, 33.85), (41.0, 33.85)),
        mcu["PH0"].left(1.1),
        mcu["PH0"],
        front=front,
        back=support_layer,
        width=SIGNAL_TRACE_MM,
    )
    _route_via_channel(
        layout,
        nets["SUPPORT/HSE_OUT"],
        crystal[3],
        (36.3, 36.0),
        ((42.8, 36.0),),
        (42.8, mcu["PH1"].y),
        mcu["PH1"],
        front=front,
        back=support_layer,
        width=SIGNAL_TRACE_MM,
    )
    _route_via_channel(
        layout,
        nets["SUPPORT/VCAP_1"],
        cvcap1.start,
        cvcap1.start.up(2.0),
        ((44.0, 40.25), (52.25, 32.0)),
        mcu["VCAP_1"].down(2.0),
        mcu["VCAP_1"],
        front=front,
        back=support_layer,
        width=SIGNAL_TRACE_MM,
    )
    _route_via_channel(
        layout,
        nets["SUPPORT/VCAP_2"],
        cvcap2.start,
        cvcap2.start.up(2.0),
        ((56.0, 40.25),),
        mcu["VCAP_2"].right(1.2),
        mcu["VCAP_2"],
        front=front,
        back=support_layer,
        width=SIGNAL_TRACE_MM,
    )

    _manual_trace(
        layout,
        nets["LED_STATUS/SUPPLY"],
        (
            led_resistor.start,
            (59.5, led_resistor.start.y),
            (59.5, 46.0),
            (65.0625, 46.0),
            (65.0625, 40.0),
        ),
        layer=front,
        width=0.20,
    )
    _via(layout, nets["LED_STATUS/SUPPLY"], at=(65.0625, 40.0), start_layer=front, end_layer=back)
    _route_via_channel(
        layout,
        nets["LED_STATUS/SIGNAL"],
        led["K"],
        led["K"].down(2.0),
        ((70.75, 54.0), (30.0, 54.0), (30.0, 34.25)),
        mcu["PC13"].left(1.2),
        mcu["PC13"],
        front=front,
        back=support_layer,
        width=SIGNAL_TRACE_MM,
    )

    support_rboot = placed["SUPPORT/RBOOT"]
    _manual_trace(
        layout,
        nets["GND"],
        (
            support_rboot.end,
            (support_boot[3].x, support_rboot.end.y),
            support_boot[3],
            (75.5, support_boot[3].y),
            (75.5, 22.5),
        ),
        layer=escape_layer,
        width=POWER_TRACE_MM,
    )
