"""Board stage for the 555 LED blinker example."""

from __future__ import annotations

import volt


def build_board(context: volt.BuildContext) -> volt.Board:
    design = context.design()
    nets = context.resource("nets", dict)
    parts = context.resource("parts", dict)

    board = design.board("555 LED Blinker")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_design_rules(
        copper_clearance=0.20,
        min_track_width=0.20,
        min_via_drill=0.30,
        min_via_annular=0.70,
        board_outline_clearance=0.25,
    )
    board.set_rectangular_outline(origin=(0.0, 0.0), size=(48.0, 32.0))
    board.add(volt.Hole(center=(4.0, 4.0), diameter=2.2, role="mounting"))
    board.add(volt.Hole(center=(44.0, 4.0), diameter=2.2, role="mounting"))
    board.add(volt.Hole(center=(4.0, 28.0), diameter=2.2, role="mounting"))
    board.add(volt.Hole(center=(44.0, 28.0), diameter=2.2, role="mounting"))
    board.add_zone(
        outline=((1.0, 1.0), (47.0, 1.0), (47.0, 31.0), (1.0, 31.0)),
        layers=(back,),
        net=nets["GND"],
    )

    with board.layout(unit=1.0, grid=0.5) as layout:
        header = layout.place(
            parts["J1"],
            at=board.edge("left").center().right(7),
            orient="right",
            locked=True,
        )
        timer = layout.place(
            parts["U1"],
            at=(23.0, 15.0),
            orient="right",
            locked=True,
        )

        with layout.hold():
            ra = (
                layout.two_pad(parts["RA"])
                .at((36.0, 10.0))
                .anchor("center")
                .right()
            )
            rb = (
                layout.two_pad(parts["RB"])
                .at((36.0, 14.0))
                .anchor("center")
                .right()
            )
            timing_junction = layout.snap(rb.end.right(2.0))
            ct = (
                layout.two_pad(parts["CT"])
                .at(timing_junction.down(3.0))
                .down()
            )

        with layout.hold():
            cctrl = (
                layout.two_pad(parts["CCTRL"])
                .at((31.0, 21.0))
                .anchor("center")
                .down()
            )
            cdec = (
                layout.two_pad(parts["CDEC"])
                .at((29.0, 10.0))
                .anchor("center")
                .right()
            )

        with layout.hold():
            rled = (
                layout.two_pad(parts["RLED"])
                .at((15.5, 20.0))
                .anchor("center")
                .down()
            )
            dled = layout.place(parts["DLED"], at=(12.5, 24.0), orient="right")

        board.add_text("555 SMD", at=(28.0, 29.5), layer=silk, size=1.0)
        board.add_text("+5V", at=(4.8, 10.2), layer=silk, size=0.7)
        board.add_text("GND", at=(7.5, 10.2), layer=silk, size=0.7)
        board.add_text("K", at=(10.2, 25.9), layer=silk, size=0.7)

        def horizontal_drop(pad, dx):
            return pad.tox(layout.snap_x(pad.x + dx))

        gnd_drops = (
            (header[2], horizontal_drop(header[2], 2.0)),
            (timer.GND, horizontal_drop(timer.GND, -2.0)),
            (cdec.end, horizontal_drop(cdec.end, 1.55)),
            (cctrl.end, horizontal_drop(cctrl.end, 1.6)),
            (ct.end, horizontal_drop(ct.end, 1.65)),
            (dled.K, horizontal_drop(dled.K, -1.6)),
        )
        for pad, drop in gnd_drops:
            layout.route(nets["GND"], layer=front, width=0.30).at(pad).to(drop)
            layout.via(
                nets["GND"],
                at=drop,
                start_layer=front,
                end_layer=back,
            )

        power_rail = layout.snap(header[1].up(5.15))
        layout.route(nets["+5V"], layer=front, width=0.30).at(header[1]).toy(
            power_rail
        ).tox(cdec.start).to(cdec.start)
        layout.route(nets["+5V"], layer=front, width=0.30).at(cdec.start).to(timer.VCC)
        layout.route(nets["+5V"], layer=front, width=0.30).at(cdec.start).toy(
            power_rail
        ).tox(ra.start).to(ra.start)
        layout.route(nets["+5V"], layer=front, width=0.25).at(timer.RESET).tox(
            timer.RESET.left(3.025)
        ).toy(timer.VCC.up(2.095)).tox(timer.VCC).to(timer.VCC)

        disch_escape = layout.snap(timer.DISCH.right(2.5))
        disch_bus = layout.snap(ra.end.right(1.6).down(2.0))
        layout.route(nets["DISCH"], layer=front).at(timer.DISCH).tox(disch_escape).to(
            rb.start
        )
        layout.route(nets["DISCH"], layer=front).at(timer.DISCH).tox(disch_escape).toy(
            disch_bus
        ).tox(disch_bus).toy(ra.end).to(ra.end)

        timing_escape = layout.snap(timer.TRIG.right(3.0))
        layout.route(nets["TIMING"], layer=front).at(timer.TRIG).tox(timing_escape).toy(
            timer.THRESH
        ).to(timer.THRESH)
        layout.route(nets["TIMING"], layer=front).at(timer.THRESH).tox(
            timing_junction
        ).toy(timing_junction).to(rb.end)
        layout.route(nets["TIMING"], layer=front).at(timing_junction).to(ct.start)

        ctrl_escape = layout.snap(timer.CTRL.right(4.5))
        layout.route(nets["CTRL"], layer=front).at(timer.CTRL).tox(ctrl_escape).toy(
            cctrl.start
        ).to(cctrl.start)
        out_escape = layout.snap(timer.OUT.right(1.5))
        layout.route(nets["OUT"], layer=front).at(timer.OUT).tox(out_escape).toy(
            rled.start
        ).to(rled.start)
        layout.route(nets["LED_A"], layer=front).at(rled.end).toy(dled.A).to(dled.A)
    return board
