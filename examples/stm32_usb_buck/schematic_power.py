"""Power-region authoring for the STM32 USB buck schematic."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib

from .schematic_symbols import (
    TWO_TERMINAL_CAPACITOR,
    TWO_TERMINAL_RESISTOR,
    _compact_connector_1x04_symbol,
    _display_reference,
    _external_supply_symbol,
)
from .stm32_board import Stm32UsbBuckBoard


def _author_power_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
) -> None:
    pwr = board.modules["PWR"]
    with region.drawing(unit=20) as drawing:
        drawing.move(dx=12, dy=42)
        power_root = drawing.here
        input_root = power_root
        input_connector_root = power_root.right(50).up(2)
        buck_root = power_root.right(198).down(8)
        regulator_3v3_root = power_root.right(386).down(6)
        analog_root = power_root.right(526).down(8)

        vin = drawing.place(
            board.components["VIN_SRC"],
            at=input_root,
            symbol=_external_supply_symbol(),
            reference_label=_display_reference(board.components["VIN_SRC"]),
        )
        pwr_j = drawing.place(
            pwr.component("J"),
            at=input_connector_root,
            symbol=_compact_connector_1x04_symbol(pin_labels=False),
            reference_label=_display_reference(pwr.component("J")),
        )
        f1 = drawing.two_terminal(
            pwr.component("F1"),
            symbol=lib.POLYFUSE.schematic_symbol,
            reference_label=_display_reference(pwr.component("F1")),
        ).at(pwr_j[1].right(34)).right()
        fb1 = drawing.two_terminal(
            pwr.component("FB1"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(pwr.component("FB1")),
        ).at(f1.end.right(10)).right()
        u5 = drawing.place(
            pwr.component("U5"),
            at=buck_root,
            symbol=lib.BUCK_MP2359.schematic_symbol,
            reference_label=_display_reference(pwr.component("U5")),
        )
        l1 = drawing.two_terminal(
            pwr.component("L1"),
            symbol=lib.POWER_INDUCTOR_10UH.schematic_symbol,
            reference_label=_display_reference(pwr.component("L1")),
        ).at(u5.SW.right(14)).right()
        dsw = drawing.two_terminal(
            pwr.component("DSW"),
            symbol=lib.SCHOTTKY_B5819.schematic_symbol,
            reference_label=_display_reference(pwr.component("DSW")),
        ).at(u5.SW.down(42).left(12)).right()
        u3v3 = drawing.place(
            pwr.component("U3V3"),
            at=regulator_3v3_root,
            symbol=lib.AP1117_33.schematic_symbol,
            reference_label=_display_reference(pwr.component("U3V3")),
        )

        cin = drawing.two_terminal(
            pwr.component("CIN"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CIN")),
        ).at(fb1.end.right(14)).down()
        cboot = drawing.two_terminal(
            pwr.component("CBST"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CBST")),
        ).at(u5.BST.right(24).up(18)).right()
        c5v = drawing.two_terminal(
            pwr.component("C5V"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C5V")),
        ).at(l1.end.right(18)).down()
        ren_top = drawing.two_terminal(
            pwr.component("REN_TOP"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(pwr.component("REN_TOP")),
        ).at(u5.EN.left(60).up(34)).down()
        ren_bottom = drawing.two_terminal(
            pwr.component("REN_BOT"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(pwr.component("REN_BOT")),
        ).at(ren_top.end.down(20)).down()
        rfb_top = drawing.two_terminal(
            pwr.component("RFB_TOP"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(pwr.component("RFB_TOP")),
        ).at(c5v.start.right(38)).down()
        rfb_bottom = drawing.two_terminal(
            pwr.component("RFB_BOT"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(pwr.component("RFB_BOT")),
        ).at(rfb_top.end.down(8)).down()
        c3v3 = drawing.two_terminal(
            pwr.component("C3V3"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C3V3")),
        ).at(u3v3.VO.right(24)).down()
        fbvdda = drawing.two_terminal(
            pwr.component("FBVDDA"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(pwr.component("FBVDDA")),
        ).at(c3v3.start.right(40)).right()
        cvdda = drawing.two_terminal(
            pwr.component("CVDDA"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CVDDA")),
        ).at(analog_root).down()

        vin.label_value(loc="bottom", ofst=10, orient="Right")
        f1.label_value(loc="bottom", ofst=8, orient="Right")
        fb1.label_value(loc="bottom", ofst=8, orient="Right")
        u5.label_value(loc="bottom", ofst=10, orient="Right")
        l1.label_value(loc="bottom", ofst=8, orient="Right")
        dsw.label_value(loc="bottom", ofst=8, orient="Right")
        u3v3.label_value(loc="bottom", ofst=16, orient="Right")
        cin.label_value(loc="bottom", ofst=16, orient="Right")
        cboot.label_value(loc="bottom", ofst=8, orient="Right")
        c5v.label_value(loc="bottom", ofst=10, orient="Right")
        ren_top.label_value(loc="bottom", ofst=8, orient="Right")
        ren_bottom.label_value(loc="bottom", ofst=8, orient="Right")
        rfb_top.label_value(loc="bottom", ofst=8, orient="Right")
        rfb_bottom.label_value(loc="bottom", ofst=8, orient="Right")
        c3v3.label_value(loc="bottom", ofst=10, orient="Right")
        fbvdda.label_value(loc="bottom", ofst=8, orient="Right")
        cvdda.label_value(loc="bottom", ofst=10, orient="Right")

        drawing.power_stub("+12V", at=vin.OUT, net=nets["+12V"], side="Up", length=20)
        drawing.ground_stub("GND", at=vin.GND, net=nets["GND"], side="Down", length=12)

        pwr_in = nets["PWR/IN_12V"]
        pwr_fused = nets["PWR/FUSED_12V"]
        pwr_buck_in = nets["PWR/BUCK_IN"]
        pwr_buck_en = nets["PWR/BUCK_EN"]
        pwr_buck_sw = nets["PWR/BUCK_SW"]
        pwr_buck_bst = nets["PWR/BUCK_BST"]
        pwr_buck_fb = nets["PWR/BUCK_FB"]
        pwr_5v = nets["PWR/OUT_5V"]
        pwr_3v3 = nets["PWR/OUT_3V3"]
        pwr_vdda = nets["PWR/VDDA"]
        pwr_gnd = nets["PWR/GND"]

        drawing.signal_tag(
            pwr_in,
            at=f1.start,
            side="Up",
            length=10,
            label="+12V IN",
        )
        drawing.connect(pwr_j[1], f1.start, net=pwr_in, shape="-")
        drawing.connect(f1.end, fb1.start, net=pwr_fused, shape="-")
        drawing.connect(fb1.end, cin.start, net=pwr_buck_in, shape="-")
        drawing.connect(cin.start, u5.IN, net=pwr_buck_in, shape="-|")
        drawing.connect(cin.start, ren_top.start, net=pwr_buck_in, shape="-|")
        drawing.net_label(pwr_buck_in, at=cin.start.up(20), label="+12V FUSED")

        drawing.connect(ren_top.end, ren_bottom.start, net=pwr_buck_en, shape="-")
        drawing.connect(ren_top.end, u5.EN, net=pwr_buck_en, shape="-|")
        drawing.connect(u5.SW, l1.start, net=pwr_buck_sw, shape="-")
        drawing.connect(l1.start, dsw.end, net=pwr_buck_sw, shape="-|")
        drawing.signal_tag(pwr_buck_sw, at=cboot.start, side="Down", length=8, label="SW")
        drawing.signal_tag(pwr_buck_bst, at=cboot.end, side="Up", length=8, label="BST")
        drawing.signal_tag(pwr_buck_bst, at=u5.BST, side="Right", length=10, label="BST")

        drawing.connect(l1.end, c5v.start, net=pwr_5v, shape="-")
        drawing.connect(c5v.start, u3v3.VI, net=pwr_5v, shape="-")
        drawing.connect(c5v.start, rfb_top.start, net=pwr_5v, shape="-|")
        drawing.power_stub("+5V", at=c5v.start, net=pwr_5v, side="Up", length=22)
        drawing.connect(rfb_top.end, rfb_bottom.start, net=pwr_buck_fb, shape="-")
        drawing.signal_tag(pwr_buck_fb, at=rfb_top.end, side="Right", length=8, label="FB")
        drawing.signal_tag(pwr_buck_fb, at=u5.FB, side="Left", length=8, label="FB")

        drawing.connect(u3v3.VO, c3v3.start, net=pwr_3v3, shape="-")
        drawing.connect(c3v3.start, fbvdda.start, net=pwr_3v3, shape="-")
        drawing.power_stub("+3V3", at=c3v3.start, net=pwr_3v3, side="Up", length=22)
        drawing.connect(fbvdda.end, cvdda.start, net=pwr_vdda, shape="-")
        drawing.power_stub("VDDA", at=cvdda.start, net=pwr_vdda, side="Up", length=20)

        connector_ground = drawing.ground(
            "GND",
            net=pwr_gnd,
            at=pwr_j[3].down(46),
            orient="Down",
        )
        for anchor in (pwr_j[2], pwr_j[3], pwr_j[4]):
            drawing.connect(anchor, connector_ground, net=pwr_gnd, shape="|-")
        drawing.junction(pwr_gnd, at=connector_ground)

        cin_ground = drawing.ground(
            "GND",
            net=pwr_gnd,
            at=cin.end.right(24),
            orient="Up",
        )
        drawing.connect(cin.end, cin_ground, net=pwr_gnd, shape="-")
        for anchor in (u5.GND, dsw.start, c5v.end, rfb_bottom.end):
            drawing.ground_stub("GND", at=anchor, net=pwr_gnd, side="Down", length=6)
        drawing.ground_stub("GND", at=ren_bottom.end, net=pwr_gnd, side="Right", length=10)

        regulator_3v3_ground = drawing.ground(
            "GND",
            net=pwr_gnd,
            at=u3v3.GND.down(24),
            orient="Down",
        )
        for anchor in (u3v3.GND, c3v3.end):
            drawing.connect(anchor, regulator_3v3_ground, net=pwr_gnd, shape="|-")
        vdda_ground = drawing.ground("GND", net=pwr_gnd, at=cvdda.end.down(18), orient="Down")
        drawing.connect(cvdda.end, vdda_ground, net=pwr_gnd, shape="-")
