"""MCU-region authoring for the STM32 USB buck schematic."""

from __future__ import annotations

import volt
from volt.libraries import stm32_usb_buck as lib

from .schematic_symbols import (
    TWO_TERMINAL_CAPACITOR,
    TWO_TERMINAL_RESISTOR,
    _compact_stm32_symbol,
    _display_reference,
    _indicator_led_symbol,
    _vertical_hse_crystal_symbol,
)
from .stm32_board import Stm32UsbBuckBoard


def _author_mcu_region(
    region: volt.SchematicRegion,
    board: Stm32UsbBuckBoard,
    nets: dict[str, volt.Net],
) -> None:
    support = board.modules["SUPPORT"]
    led = board.modules["LED_STATUS"]
    with region.drawing(unit=20) as drawing:
        drawing.move(dx=104, dy=64)
        stm32 = drawing.place(
            board.components["U1"],
            symbol=_compact_stm32_symbol(),
            reference_label=_display_reference(board.components["U1"]),
        )

        decoupling_start = stm32.VBAT.left(66).up(50)
        cvdd_anchor, cvcap1_anchor, cvcap2_anchor = drawing.stack(
            count=3,
            direction="Right",
            pitch=30,
            at=decoupling_start,
        )
        cvdd = drawing.two_terminal(
            support.component("CVDD"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVDD")),
        ).at(cvdd_anchor).down()
        cvcap1 = drawing.two_terminal(
            support.component("CVCAP1"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVCAP1")),
        ).at(cvcap1_anchor).down()
        cvcap2 = drawing.two_terminal(
            support.component("CVCAP2"),
            symbol=TWO_TERMINAL_CAPACITOR,
            reference_label=_display_reference(support.component("CVCAP2")),
        ).at(cvcap2_anchor).down()

        rreset = drawing.two_terminal(
            support.component("RRESET"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(support.component("RRESET")),
        ).at(stm32.NRST.left(76).down(38)).down(1.1)

        drawing.move_from(stm32.BOOT0.right(42).down(18), direction="Right")
        swboot = drawing.place(
            support.component("SWBOOT"),
            orient="Right",
            symbol=lib.SPDT_SWITCH.schematic_symbol,
            reference_label=_display_reference(support.component("SWBOOT")),
        )
        rboot = drawing.two_terminal(
            support.component("RBOOT"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(support.component("RBOOT")),
        ).at(swboot.C.right(8)).down(1.1)

        drawing.move_from(stm32.PH0.left(70).up(2), direction="Right")
        crystal = drawing.place(
            support.component("Y1"),
            symbol=_vertical_hse_crystal_symbol(),
            reference_label=_display_reference(support.component("Y1")),
        )
        chsein = drawing.two_terminal(
            support.component("CHSEIN"),
            symbol=TWO_TERMINAL_CAPACITOR,
        ).at(crystal[1].left(66)).down()
        chseout = drawing.two_terminal(
            support.component("CHSEOUT"),
            symbol=TWO_TERMINAL_CAPACITOR,
        ).at(crystal[3].left(54)).down()

        led_r = drawing.two_terminal(
            led.component("R"),
            symbol=TWO_TERMINAL_RESISTOR,
            reference_label=_display_reference(led.component("R")),
        ).at(stm32.PA11.right(60).up(44)).right()
        led_d = drawing.two_terminal(
            led.component("D"),
            symbol=_indicator_led_symbol(),
            reference_label=_display_reference(led.component("D")),
        ).at(led_r.end.right(6)).right(1.2)

        stm32.label_value(loc="bottom", ofst=12, orient="Right")
        cvdd.label_value(loc="bottom", ofst=8, orient="Right")
        cvcap1.label_value(loc="bottom", ofst=8, orient="Right")
        cvcap2.label_value(loc="bottom", ofst=8, orient="Right")
        rreset.label_value(loc="bottom", ofst=10, orient="Right")
        rboot.label_value(loc="bottom", ofst=10, orient="Right")
        swboot.label_value(loc="bottom", ofst=10, orient="Right")
        crystal.label_value(loc="bottom", ofst=22, orient="Right")
        chsein.label(
            _display_reference(support.component("CHSEIN")),
            name="reference",
            loc="top",
            ofst=8,
        )
        chsein.label_value(loc="bottom", ofst=8, orient="Right")
        chseout.label(
            _display_reference(support.component("CHSEOUT")),
            name="reference",
            loc="top",
            ofst=8,
        )
        chseout.label_value(loc="bottom", ofst=8, orient="Right")
        led_r.label_value(loc="bottom", ofst=10, orient="Right")
        led_d.label_value(loc="bottom", ofst=8, orient="Right")

        mcu_3v3 = nets["+3V3"]
        mcu_power = drawing.power("+3V3", net=mcu_3v3, at=stm32.center.up(98), orient="Up")
        for anchor in (*stm32.pins("VDD"), stm32.VBAT):
            drawing.connect(anchor, mcu_power, net=mcu_3v3, shape="|-")
        mcu_vdda = drawing.power("VDDA", net=nets["VDDA"], at=stm32.VDDA.left(16), orient="Left")
        drawing.connect(stm32.VDDA, mcu_vdda, net=nets["VDDA"], shape="-")

        mcu_ground = drawing.ground(
            "GND",
            net=nets["GND"],
            at=stm32.VSSA.down(20),
            orient="Down",
        )
        for anchor in (*stm32.pins("VSS"), stm32.VSSA):
            drawing.connect(anchor, mcu_ground, net=nets["GND"], shape="|-")
        drawing.junction(nets["GND"], at=mcu_ground)

        drawing.signal_tags(
            (
                (nets["MCU_USB_DM"], stm32.PA11, "USB D-"),
                (nets["MCU_USB_DP"], stm32.PA12, "USB D+"),
                (nets["SWDIO"], stm32.PA13, "SWDIO"),
                (nets["SWCLK"], stm32.PA14, "SWCLK"),
                (nets["SWO"], stm32.PB3, "SWO"),
                (nets["BOOT0"], stm32.BOOT0, "BOOT0"),
            ),
            length=10,
        )
        for net, anchor, label, tag_length, tag_orient in (
            (nets["STATUS_LED"], stm32.PC13, "LED", 16, None),
            (nets["HSE_IN"], stm32.PH0, "HSE IN", 6, None),
            (nets["HSE_OUT"], stm32.PH1, "HSE OUT", 6, None),
            (nets["VCAP_1"], stm32.VCAP_1, "VCAP1", 16, None),
        ):
            drawing.signal_tag(
                net,
                at=anchor,
                side="Left",
                length=tag_length,
                label=label,
                orient=tag_orient,
            )
        drawing.signal_tag(
            nets["NRST"],
            at=stm32.NRST,
            side="Left",
            length=8,
            label="NRST",
        )
        drawing.signal_tag(
            nets["VCAP_2"],
            at=stm32.VCAP_2,
            side="Right",
            length=10,
            label="VCAP2",
        )

        support_vdd = nets["SUPPORT/VDD"]
        support_gnd = nets["SUPPORT/GND"]
        support_reset = nets["SUPPORT/NRST"]
        support_boot = nets["SUPPORT/BOOT0"]
        support_hse_in = nets["SUPPORT/HSE_IN"]
        support_hse_out = nets["SUPPORT/HSE_OUT"]

        drawing.power_stub("+3V3", at=cvdd.start, net=support_vdd, side="Up", length=16)
        drawing.power_stub(
            "+3V3",
            at=rreset.start,
            net=support_vdd,
            side="Up",
            length=6,
        )
        drawing.power_stub("+3V3", at=swboot.A, net=support_vdd, side="Left", length=12, orient="Up")

        decoupling_ground = drawing.ground("GND", net=support_gnd, at=cvcap1.end.down(18), orient="Down")
        for cap in (cvdd, cvcap1, cvcap2):
            drawing.connect(cap.end, decoupling_ground, net=support_gnd, shape="|-")
        drawing.junction(support_gnd, at=decoupling_ground)

        oscillator_ground = drawing.ground("GND", net=support_gnd, at=crystal[4].down(22), orient="Down")
        for anchor in (crystal[2], crystal[4], chsein.end, chseout.end):
            drawing.connect(anchor, oscillator_ground, net=support_gnd, shape="|-")
        drawing.junction(support_gnd, at=oscillator_ground)

        drawing.ground_stub("GND", at=rboot.end, net=support_gnd, side="Down", length=6, orient="Down")
        drawing.ground_stub("GND", at=swboot.B, net=support_gnd, side="Down", length=6, orient="Down")

        drawing.signal_tag(
            support_reset,
            at=rreset.end,
            side="Right",
            length=10,
            label="NRST",
        )
        drawing.connect(swboot.C, rboot.start, net=support_boot, shape="-")
        drawing.signal_tag(
            support_boot,
            at=rboot.start,
            side="Up",
            length=10,
            label="BOOT0",
        )

        drawing.connect(crystal[1], chsein.start, net=support_hse_in, shape="-")
        drawing.connect(crystal[3], chseout.start, net=support_hse_out, shape="-")
        drawing.net_label(
            support_hse_in,
            at=crystal[1].right(8).up(8),
            label="HSE IN",
            orient="Right",
        )
        drawing.net_label(
            support_hse_out,
            at=crystal[3].right(8).down(8),
            label="HSE OUT",
            orient="Right",
        )
        for cap, net, label in (
            (cvcap1, nets["SUPPORT/VCAP_1"], "VCAP1"),
            (cvcap2, nets["SUPPORT/VCAP_2"], "VCAP2"),
        ):
            stub_anchor = cap.start.up(12)
            drawing.connect(cap.start, stub_anchor, net=net, shape="-")
            drawing.signal_tag(
                net,
                at=stub_anchor,
                side="Right",
                length=12,
                label=label,
            )

        led_supply = nets["LED_STATUS/SUPPLY"]
        led_anode = nets["LED_STATUS/LED_A"]
        led_signal = nets["LED_STATUS/SIGNAL"]
        drawing.power_stub("+3V3", at=led_r.start, net=led_supply, side="Up", length=20)
        drawing.connect(led_r.end, led_d.start, net=led_anode, shape="-")
        drawing.signal_tag(
            led_signal,
            at=led_d.end,
            side="Right",
            length=16,
            label="LED",
        )
