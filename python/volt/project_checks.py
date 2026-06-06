"""Product-intent assertion helpers for Volt project stage tests."""

from __future__ import annotations

import json

from .design import Design
from .pcb import Board
from .schematic import Schematic


class DesignCheck:
    """Product-intent assertions over a logical design."""

    def __init__(self, design: Design):
        self._design = design

    def ok(self, _message: str = "") -> None:
        """Allow smoke tests that only need to run the stage."""
        return None

    def net(self, name: str) -> "NetCheck":
        """Return assertions for one logical net."""
        return NetCheck(self._design, name)

    def no_connection(self, first: str, second: str) -> None:
        """Assert two logical nets do not share any connected pin labels."""
        first_pins = set(self.net(first)._pin_labels())
        second_pins = set(self.net(second)._pin_labels())
        shared = sorted(first_pins & second_pins)
        if shared:
            raise AssertionError(
                f"Nets {first} and {second} unexpectedly share pins: {', '.join(shared)}"
            )


class NetCheck:
    """Product-intent assertions over one logical net."""

    def __init__(self, design: Design, net_name: str):
        self._design = design
        self._net_name = net_name

    def connects(self, *pins: str) -> None:
        """Assert this net connects every requested component pin label."""
        connected = set(self._pin_labels())
        missing = [pin for pin in pins if pin not in connected]
        if missing:
            raise AssertionError(
                f"Net {self._net_name} is missing expected pins: {', '.join(missing)}"
            )

    def _pin_labels(self) -> tuple[str, ...]:
        net = next((item for item in self._design.nets() if item.name == self._net_name), None)
        if net is None:
            raise AssertionError(f"Design has no net named {self._net_name}")

        labels: list[str] = []
        for pin in net.pins():
            labels.append(f"{pin.component_reference}.{pin.name}")
            labels.append(f"{pin.component_reference}.{pin.number}")
        return tuple(labels)


class SchematicCheck:
    """Product-intent assertions over a schematic projection."""

    def __init__(self, schematic: Schematic):
        self._schematic = schematic

    def places(self, *references: str) -> None:
        """Assert this schematic places every requested component reference."""
        document = json.loads(self._schematic.to_json())
        placed_components = {item["component"] for item in document["symbol_instances"]}
        missing = [
            reference
            for reference in references
            if _component_id_by_reference(self._schematic._design, reference)
            not in placed_components
        ]
        if missing:
            raise AssertionError(
                f"Schematic {self._schematic.name} is missing placed components: "
                + ", ".join(missing)
            )


class BoardCheck:
    """Product-intent assertions over a PCB projection."""

    def __init__(self, board: Board):
        self._board = board

    def has_outline(self) -> None:
        """Assert this board has a non-empty mechanical outline."""
        document = json.loads(self._board.to_json())
        outline = document["board"].get("outline", {})
        if not outline.get("vertices"):
            raise AssertionError(f"Board {self._board.name} has no outline")

    def places(self, *references: str) -> None:
        """Assert this board places every requested component reference."""
        document = json.loads(self._board.to_json())
        placed_components = {item["component"] for item in document["board"]["placements"]}
        missing = [
            reference
            for reference in references
            if _component_id_by_reference(self._board._design, reference)
            not in placed_components
        ]
        if missing:
            raise AssertionError(
                f"Board {self._board.name} is missing placed components: "
                + ", ".join(missing)
            )


def _component_id_by_reference(design: Design, reference: str) -> str:
    try:
        component = design.component(reference)
    except KeyError as error:
        raise AssertionError(f"Design {design.name} has no component {reference}") from error
    return f"component:{component.index}"
