"""Named board-level power nets for the STM32 USB buck example."""

from __future__ import annotations

from dataclasses import dataclass

import volt


@dataclass
class PowerNets:
    input_12v: volt.Net
    usb_5v: volt.Net
    logic_3v3: volt.Net
    analog_3v3: volt.Net
    ground: volt.Net


def create_power_nets(design: volt.Design) -> PowerNets:
    return PowerNets(
        input_12v=design.net("+12V", kind="power", voltage=12.0),
        usb_5v=design.net("+5V", kind="power", voltage=5.0),
        logic_3v3=design.net("+3V3", kind="power", voltage=3.3),
        analog_3v3=design.net("VDDA", kind="power", voltage=3.3),
        ground=design.net("GND", kind="ground"),
    )
