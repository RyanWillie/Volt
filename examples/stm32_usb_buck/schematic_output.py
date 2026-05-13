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

    schematic = board.design.schematic("Main")
    pwr = board.modules["PWR"]
    usb = board.modules["USB"]
    support = board.modules["SUPPORT"]
    led = board.modules["LED_STATUS"]

    schematic.place(board.components["VIN_SRC"], at=(12, 34), symbol=_external_supply_symbol())
    schematic.place(pwr.component("J"), at=(12, 82), symbol=lib.CONNECTOR_1X04.schematic_symbol)
    schematic.place(pwr.component("U5"), at=(68, 42), symbol=lib.AP1117_15.schematic_symbol)
    schematic.place(pwr.component("U3V3"), at=(68, 86), symbol=lib.AP1117_15.schematic_symbol)
    schematic.place(pwr.component("CIN"), at=(24, 130), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(pwr.component("C5V"), at=(52, 130), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(pwr.component("C3V3"), at=(80, 130), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(pwr.component("CVDDA"), at=(108, 130), symbol=lib.CAPACITOR.schematic_symbol)

    schematic.place(usb.component("J1"), at=(198, 30), symbol=lib.USB_B_MICRO.schematic_symbol)
    schematic.place(usb.component("U1"), at=(226, 74), symbol=lib.USBLC6_4SC6.schematic_symbol)

    schematic.place(board.components["U1"], at=(132, 24), symbol=lib.STM32F405RGTx.schematic_symbol)
    schematic.place(support.component("CVDD"), at=(104, 28), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(support.component("CVCAP1"), at=(104, 58), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(support.component("CVCAP2"), at=(104, 88), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(support.component("RRESET"), at=(104, 120), symbol=lib.RESISTOR.schematic_symbol)
    schematic.place(support.component("RBOOT"), at=(104, 148), symbol=lib.RESISTOR.schematic_symbol)
    schematic.place(support.component("SWBOOT"), at=(160, 176), symbol=lib.SPDT_SWITCH.schematic_symbol)
    schematic.place(support.component("Y1"), at=(206, 142), symbol=lib.CRYSTAL_GND24.schematic_symbol)
    schematic.place(support.component("CHSEIN"), at=(208, 176), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(support.component("CHSEOUT"), at=(236, 176), symbol=lib.CAPACITOR.schematic_symbol)

    schematic.place(board.components["J2"], at=(252, 18), symbol=lib.JTAG_SWD_10.schematic_symbol)
    schematic.place(board.components["J3"], at=(252, 110), symbol=lib.CONNECTOR_1X04.schematic_symbol)
    schematic.place(led.component("R"), at=(18, 176), symbol=lib.RESISTOR.schematic_symbol)
    schematic.place(led.component("D"), at=(54, 176), symbol=_indicator_led_symbol())

    _add_labelled_net_stubs(schematic, board.nets)
    _add_pin_anchor_net_labels(schematic, board)
    return schematic


def _add_pin_anchor_net_labels(schematic: volt.Schematic, board: Stm32UsbBuckBoard) -> None:
    """Add local net labels at connected symbol pins so readiness validation can prove coverage."""

    logical = json.loads(board.design.to_json())
    projection = json.loads(schematic.to_json())
    nets_by_id = {
        net["id"]: volt.Net(
            board.design,
            int(net["id"].removeprefix("net:")),
            net["name"],
        )
        for net in logical["nets"]
    }
    component_definitions = {
        definition["id"]: definition for definition in logical["component_definitions"]
    }
    pin_definitions = {pin["id"]: pin for pin in logical["pin_definitions"]}
    components = {component["id"]: component for component in logical["components"]}
    pins_by_component_and_definition = {
        (pin["component"], pin["definition"]): pin["id"] for pin in logical["pins"]
    }
    net_by_pin = {
        pin_id: net["id"] for net in logical["nets"] for pin_id in net.get("pins", [])
    }
    symbols = {
        symbol["id"]: symbol for symbol in projection["symbol_definitions"]
    }

    for instance in projection["symbol_instances"]:
        component = components[instance["component"]]
        component_definition = component_definitions[component["definition"]]
        pin_definition_by_number = {
            pin_definitions[pin_id]["number"]: pin_id for pin_id in component_definition["pins"]
        }
        symbol = symbols[instance["symbol_definition"]]
        for symbol_pin in symbol["pins"]:
            pin_definition_id = pin_definition_by_number.get(symbol_pin["number"])
            if pin_definition_id is None:
                continue
            pin_id = pins_by_component_and_definition.get((component["id"], pin_definition_id))
            if pin_id is None:
                continue
            net_id = net_by_pin.get(pin_id)
            if net_id not in nets_by_id:
                continue
            schematic.label(
                nets_by_id[net_id],
                at=_transformed_pin_anchor(symbol_pin["anchor"], instance),
            )


def _transformed_pin_anchor(anchor: dict, instance: dict) -> tuple[float, float]:
    x = anchor["x"]
    y = anchor["y"]
    orientation = instance["orientation"]
    if orientation == "Right":
        dx, dy = x, y
    elif orientation == "Down":
        dx, dy = -y, x
    elif orientation == "Left":
        dx, dy = -x, -y
    elif orientation == "Up":
        dx, dy = y, -x
    else:
        raise ValueError(f"unknown schematic orientation {orientation!r}")
    return (instance["position"]["x"] + dx, instance["position"]["y"] + dy)


def _add_labelled_net_stubs(schematic: volt.Schematic, nets: dict[str, volt.Net]) -> None:
    lanes = (
        ("+12V", 8, 44, 18),
        ("+5V", 52, 88, 18),
        ("+3V3", 96, 132, 18),
        ("VDDA", 8, 44, 202),
        ("GND", 52, 88, 202),
        ("USB_DP", 180, 216, 18),
        ("USB_DM", 224, 260, 18),
        ("MCU_USB_DP", 180, 216, 126),
        ("MCU_USB_DM", 224, 260, 126),
        ("NRST", 8, 44, 164),
        ("BOOT0", 52, 88, 164),
        ("HSE_IN", 180, 216, 166),
        ("HSE_OUT", 224, 260, 166),
        ("VCAP_1", 8, 44, 194),
        ("VCAP_2", 96, 132, 194),
        ("SWDIO", 252, 288, 96),
        ("SWCLK", 252, 288, 104),
        ("SWO", 252, 288, 136),
        ("STATUS_LED", 8, 44, 156),
    )
    for name, x1, x2, y in lanes:
        net = nets[name]
        schematic.wire(net, ((x1, y), (x2, y)))
        schematic.label(net, at=(x1, y - 2))
