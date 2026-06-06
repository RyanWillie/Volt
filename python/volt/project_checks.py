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

    @property
    def name(self) -> str:
        """Return the stable design name for this check target."""
        return self._design.name

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
                f"Design {self.name} nets {first} and {second} unexpectedly share pins: "
                f"{', '.join(shared)}"
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
                f"Design {self._design.name} net {self._net_name} is missing expected "
                f"pins: {', '.join(missing)}"
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

    @property
    def name(self) -> str:
        """Return the stable schematic lookup name for this check target."""
        return _projection_name(self._schematic)

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
                f"Schematic {self.name} is missing placed components: "
                + ", ".join(missing)
            )


class BoardCheck:
    """Product-intent assertions over a PCB projection."""

    def __init__(self, board: Board):
        self._board = board

    @property
    def name(self) -> str:
        """Return the stable board lookup name for this check target."""
        return _projection_name(self._board)

    def has_outline(self) -> None:
        """Assert this board has a non-empty mechanical outline."""
        document = json.loads(self._board.to_json())
        outline = document["board"].get("outline", {})
        if not outline.get("vertices"):
            raise AssertionError(f"Board {self.name} has no outline")

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
                f"Board {self.name} is missing placed components: "
                + ", ".join(missing)
            )


class DesignStageChecks:
    """Explicit multi-model helpers for a design stage test."""

    def __init__(self, designs: tuple[Design, ...]):
        self._checks = tuple(DesignCheck(design) for design in designs)

    def ok(self, _message: str = "") -> None:
        """Allow smoke tests that only need to run the stage."""
        return None

    def names(self) -> tuple[str, ...]:
        """Return deterministic design names in stage return order."""
        return tuple(check.name for check in self._checks)

    def design(self, name: str | None = None) -> DesignCheck:
        """Return one design check by name, or the only design."""
        return _one_or_named_check(self._checks, name, "design")

    def designs(self) -> tuple[DesignCheck, ...]:
        """Return design checks in deterministic stage return order."""
        return self._checks


class SchematicStageChecks:
    """Explicit multi-model helpers for a schematic stage test."""

    def __init__(self, schematics: tuple[Schematic, ...]):
        self._checks = tuple(SchematicCheck(schematic) for schematic in schematics)

    def ok(self, _message: str = "") -> None:
        """Allow smoke tests that only need to run the stage."""
        return None

    def names(self) -> tuple[str, ...]:
        """Return deterministic schematic names in stage return order."""
        return tuple(check.name for check in self._checks)

    def schematic(self, name: str | None = None) -> SchematicCheck:
        """Return one schematic check by name, or the only schematic."""
        return _one_or_named_check(self._checks, name, "schematic")

    def schematics(self) -> tuple[SchematicCheck, ...]:
        """Return schematic checks in deterministic stage return order."""
        return self._checks


class BoardStageChecks:
    """Explicit multi-model helpers for a board stage test."""

    def __init__(self, boards: tuple[Board, ...]):
        self._checks = tuple(BoardCheck(board) for board in boards)

    def ok(self, _message: str = "") -> None:
        """Allow smoke tests that only need to run the stage."""
        return None

    def names(self) -> tuple[str, ...]:
        """Return deterministic board names in stage return order."""
        return tuple(check.name for check in self._checks)

    def board(self, name: str | None = None) -> BoardCheck:
        """Return one board check by name, or the only board."""
        return _one_or_named_check(self._checks, name, "board")

    def boards(self) -> tuple[BoardCheck, ...]:
        """Return board checks in deterministic stage return order."""
        return self._checks


def _component_id_by_reference(design: Design, reference: str) -> str:
    try:
        component = design.component(reference)
    except KeyError as error:
        raise AssertionError(f"Design {design.name} has no component {reference}") from error
    return f"component:{component.index}"


def _one_or_named_check(checks: tuple[object, ...], name: str | None, kind: str):
    if name is None:
        if len(checks) != 1:
            raise LookupError(f"Project stage test has {len(checks)} {kind} models")
        return checks[0]
    matches = [check for check in checks if check.name == name]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        raise LookupError(f"Project stage test has multiple {kind} models named {name}")
    raise LookupError(f"Project stage test has no {kind} named {name}")


def _projection_name(model: Board | Schematic) -> str:
    return f"{_projection_key_part(model._design.name)}:{_projection_key_part(model.name)}"


def _projection_key_part(name: str) -> str:
    return name.replace("~", "~0").replace(":", "~1")
