"""Power-region authoring for the STM32 USB buck schematic."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib

from .schematic_symbols import (
    TWO_TERMINAL_CAPACITOR,
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
        input_connector_root = power_root.right(58).up(2)
        regulator_5v_root = power_root.right(150).up(4)
        regulator_3v3_root = power_root.right(318).up(4)
        analog_root = power_root.right(496).down(8)

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

        u5 = drawing.place(
            pwr.component("U5"),
            at=regulator_5v_root,
            symbol=lib.AP1117_15.schematic_symbol,
            reference_label=_display_reference(pwr.component("U5")),
        )
        u3v3 = drawing.place(
            pwr.component("U3V3"),
            at=regulator_3v3_root,
            symbol=lib.AP1117_15.schematic_symbol,
            reference_label=_display_reference(pwr.component("U3V3")),
        )

        cin = drawing.two_terminal(
            pwr.component("CIN"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CIN")),
        ).at(u5.VI.left(24)).down()
        c5v = drawing.two_terminal(
            pwr.component("C5V"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C5V")),
        ).at(u5.VO.right(24)).down()
        c3v3 = drawing.two_terminal(
            pwr.component("C3V3"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("C3V3")),
        ).at(u3v3.VO.right(24)).down()
        cvdda = drawing.two_terminal(
            pwr.component("CVDDA"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(pwr.component("CVDDA")),
        ).at(analog_root).down()

        vin.label_value(loc="bottom", ofst=10, orient="Right")
        u5.label_value(loc="bottom", ofst=10, orient="Right")
        u3v3.label_value(loc="bottom", ofst=16, orient="Right")
        cin.label_value(loc="bottom", ofst=16, orient="Right")
        c5v.label_value(loc="bottom", ofst=10, orient="Right")
        c3v3.label_value(loc="bottom", ofst=10, orient="Right")
        cvdda.label_value(loc="bottom", ofst=10, orient="Right")

        drawing.power_stub("+12V", at=vin.OUT, net=nets["+12V"], side="Up", length=20)
        drawing.ground_stub("GND", at=vin.GND, net=nets["GND"], side="Down", length=24)

        pwr_in = nets["PWR/IN_12V"]
        pwr_5v = nets["PWR/OUT_5V"]
        pwr_3v3 = nets["PWR/OUT_3V3"]
        pwr_vdda = nets["PWR/VDDA"]
        pwr_gnd = nets["PWR/GND"]

        drawing.signal_tag(
            pwr_in,
            at=pwr_j[1],
            side="Up",
            length=10,
            label="+12V",
        )
        drawing.connect(cin.start, u5.VI, net=pwr_in, shape="-")
        drawing.net_label(pwr_in, at=cin.start.up(20), label="+12V")

        drawing.connect(u5.VO, c5v.start, net=pwr_5v, shape="-")
        drawing.connect(c5v.start, u3v3.VI, net=pwr_5v, shape="-")
        drawing.power_stub("+5V", at=c5v.start, net=pwr_5v, side="Up", length=22)

        drawing.connect(u3v3.VO, c3v3.start, net=pwr_3v3, shape="-")
        drawing.power_stub("+3V3", at=c3v3.start, net=pwr_3v3, side="Up", length=22)
        drawing.power_stub("VDDA", at=cvdda.start, net=pwr_vdda, side="Up", length=20)

        connector_ground = drawing.ground(
            "GND",
            net=pwr_gnd,
            at=pwr_j[3].down(30),
            orient="Down",
        )
        for anchor in (pwr_j[2], pwr_j[3], pwr_j[4]):
            drawing.connect(anchor, connector_ground, net=pwr_gnd, shape="|-")
        drawing.junction(pwr_gnd, at=connector_ground)

        regulator_5v_ground = drawing.ground(
            "GND",
            net=pwr_gnd,
            at=u5.GND.down(24),
            orient="Down",
        )
        for anchor in (cin.end, u5.GND, c5v.end):
            drawing.connect(anchor, regulator_5v_ground, net=pwr_gnd, shape="|-")
        drawing.junction(pwr_gnd, at=regulator_5v_ground)

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
