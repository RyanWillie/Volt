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


def _mounting_hole_symbol() -> volt.SchematicSymbolSpec:
    return volt.SchematicSymbolSpec(
        "volt.examples.stm32_usb_buck:MountingHolePad",
        pins=(volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),),
        primitives=(
            volt.SchematicSymbolSpec.line((0, 0), (10, 0)),
            volt.SchematicSymbolSpec.circle((16, 0), 5),
            volt.SchematicSymbolSpec.circle((16, 0), 2),
            volt.SchematicSymbolSpec.text("HOLE", (16, -9)),
        ),
    )


def build_schematic(board: Stm32UsbBuckBoard) -> volt.Schematic:
    """Create a deterministic, manually authored schematic projection."""

    power = board.design.schematic("Power")
    mcu = board.design.schematic("STM32 Microcontroller")
    connectors = board.design.schematic("Connectors and USB")

    _place_power_sheet(power, board)
    _place_mcu_sheet(mcu, board)
    _place_connectors_sheet(connectors, board)

    _add_pin_net_stubs(power, board)
    _add_pin_net_stubs(mcu, board)
    _add_pin_net_stubs(connectors, board)
    return power


def _place_power_sheet(schematic: volt.Schematic, board: Stm32UsbBuckBoard) -> None:
    pwr = board.modules["PWR"]

    schematic.place(board.components["VIN_SRC"], at=(70, 70), symbol=_external_supply_symbol())
    schematic.place(pwr.component("J"), at=(110, 180), symbol=lib.CONNECTOR_1X04.schematic_symbol)
    schematic.place(pwr.component("U5"), at=(210, 90), symbol=lib.AP1117_15.schematic_symbol)
    schematic.place(pwr.component("U3V3"), at=(360, 90), symbol=lib.AP1117_15.schematic_symbol)
    schematic.place(pwr.component("CIN"), at=(140, 270), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(pwr.component("C5V"), at=(260, 270), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(pwr.component("C3V3"), at=(400, 270), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(pwr.component("CVDDA"), at=(500, 270), symbol=lib.CAPACITOR.schematic_symbol)


def _place_mcu_sheet(schematic: volt.Schematic, board: Stm32UsbBuckBoard) -> None:
    support = board.modules["SUPPORT"]
    led = board.modules["LED_STATUS"]

    schematic.place(board.components["U1"], at=(220, 90), symbol=lib.STM32F405RGTx.schematic_symbol)
    schematic.place(support.component("CVDD"), at=(80, 80), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(support.component("CVCAP1"), at=(125, 80), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(support.component("CVCAP2"), at=(170, 80), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(support.component("RRESET"), at=(80, 175), symbol=lib.RESISTOR.schematic_symbol)
    schematic.place(support.component("RBOOT"), at=(80, 230), symbol=lib.RESISTOR.schematic_symbol)
    schematic.place(support.component("SWBOOT"), at=(440, 100), symbol=lib.SPDT_SWITCH.schematic_symbol)
    schematic.place(support.component("Y1"), at=(430, 245), symbol=lib.CRYSTAL_GND24.schematic_symbol)
    schematic.place(support.component("CHSEIN"), at=(430, 315), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(support.component("CHSEOUT"), at=(500, 315), symbol=lib.CAPACITOR.schematic_symbol)
    schematic.place(led.component("R"), at=(80, 345), symbol=lib.RESISTOR.schematic_symbol)
    schematic.place(led.component("D"), at=(155, 345), symbol=_indicator_led_symbol())


def _place_connectors_sheet(schematic: volt.Schematic, board: Stm32UsbBuckBoard) -> None:
    usb = board.modules["USB"]

    schematic.place(usb.component("J1"), at=(95, 250), symbol=lib.USB_B_MICRO.schematic_symbol)
    schematic.place(usb.component("U1"), at=(275, 255), symbol=lib.USBLC6_4SC6.schematic_symbol)
    schematic.place(board.components["J2"], at=(360, 75), symbol=lib.JTAG_SWD_10.schematic_symbol)
    schematic.place(board.components["J3"], at=(360, 265), symbol=lib.CONNECTOR_1X04.schematic_symbol)
    schematic.place(board.components["H1"], at=(500, 105), symbol=_mounting_hole_symbol())
    schematic.place(board.components["H2"], at=(500, 155), symbol=_mounting_hole_symbol())
    schematic.place(board.components["H3"], at=(500, 205), symbol=_mounting_hole_symbol())
    schematic.place(board.components["H4"], at=(500, 255), symbol=_mounting_hole_symbol())


def _add_pin_net_stubs(schematic: volt.Schematic, board: Stm32UsbBuckBoard) -> None:
    """Draw local labelled wires from each connected placed pin."""

    logical = json.loads(board.design.to_json())
    projection = json.loads(schematic.to_json())
    sheet_id = f"sheet:{schematic.sheet_index}"
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
        if instance["sheet"] != sheet_id:
            continue
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
            _wire_pin_stub(
                schematic,
                nets_by_id[net_id],
                _transformed_pin_anchor(symbol_pin["anchor"], instance),
                symbol_pin["orientation"],
            )


def _wire_pin_stub(
    schematic: volt.Schematic,
    net: volt.Net,
    anchor: tuple[float, float],
    orientation: str,
) -> None:
    x, y = anchor
    if orientation == "Left":
        end = (x - 42, y)
        label_at = (x - 72, y - 3)
    elif orientation == "Right":
        end = (x + 42, y)
        label_at = (x + 46, y - 3)
    elif orientation == "Up":
        end = (x, y - 34)
        label_at = (x + 5, y - 40)
    elif orientation == "Down":
        end = (x, y + 34)
        label_at = (x + 5, y + 42)
    else:
        raise ValueError(f"unknown schematic orientation {orientation!r}")

    schematic.wire(net, (anchor, end))
    schematic.label(net, at=label_at)


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
