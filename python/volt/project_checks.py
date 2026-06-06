"""Product-intent assertion helpers for Volt project stage tests."""

from __future__ import annotations

import json

from ._project_model_lookup import model_output_name, one_or_named, one_or_named_projection
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

    def __init__(
        self,
        schematic: Schematic,
        *,
        siblings: tuple[Schematic, ...] | None = None,
    ):
        self._schematic = schematic
        self._siblings = siblings or (schematic,)

    @property
    def name(self) -> str:
        """Return the stable schematic lookup name for this check target."""
        return model_output_name(self._schematic, self._siblings)

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

    def __init__(
        self,
        board: Board,
        *,
        siblings: tuple[Board, ...] | None = None,
    ):
        self._board = board
        self._siblings = siblings or (board,)

    @property
    def name(self) -> str:
        """Return the stable board lookup name for this check target."""
        return model_output_name(self._board, self._siblings)

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
        self._designs = tuple(designs)
        self._checks = tuple(DesignCheck(design) for design in designs)
        self._checks_by_id = {id(design): check for design, check in zip(self._designs, self._checks)}

    def ok(self, _message: str = "") -> None:
        """Allow smoke tests that only need to run the stage."""
        return None

    def names(self) -> tuple[str, ...]:
        """Return deterministic design names in stage return order."""
        return tuple(design.name for design in self._designs)

    def design(self, name: str | None = None) -> DesignCheck:
        """Return one design check by name, or the only design."""
        design = one_or_named(self._designs, name, "design", "Project stage test")
        return self._checks_by_id[id(design)]

    def designs(self) -> tuple[DesignCheck, ...]:
        """Return design checks in deterministic stage return order."""
        return self._checks

    def net(self, name: str) -> NetCheck:
        """Return assertions for one logical net on the only design."""
        return self.design().net(name)

    def no_connection(self, first: str, second: str) -> None:
        """Assert two logical nets are not connected on the only design."""
        self.design().no_connection(first, second)


class SchematicStageChecks:
    """Explicit multi-model helpers for a schematic stage test."""

    def __init__(self, schematics: tuple[Schematic, ...]):
        self._schematics = tuple(schematics)
        self._checks = tuple(
            SchematicCheck(schematic, siblings=self._schematics)
            for schematic in self._schematics
        )
        self._checks_by_id = {
            id(schematic): check
            for schematic, check in zip(self._schematics, self._checks)
        }

    def ok(self, _message: str = "") -> None:
        """Allow smoke tests that only need to run the stage."""
        return None

    def names(self) -> tuple[str, ...]:
        """Return deterministic schematic names in stage return order."""
        return tuple(
            model_output_name(schematic, self._schematics)
            for schematic in self._schematics
        )

    def schematic(self, name: str | None = None) -> SchematicCheck:
        """Return one schematic check by name, or the only schematic."""
        schematic = one_or_named_projection(
            self._schematics,
            name,
            "schematic",
            "Project stage test",
        )
        return self._checks_by_id[id(schematic)]

    def schematics(self) -> tuple[SchematicCheck, ...]:
        """Return schematic checks in deterministic stage return order."""
        return self._checks

    def places(self, *references: str) -> None:
        """Assert the only schematic places every requested component."""
        self.schematic().places(*references)


class BoardStageChecks:
    """Explicit multi-model helpers for a board stage test."""

    def __init__(self, boards: tuple[Board, ...]):
        self._boards = tuple(boards)
        self._checks = tuple(
            BoardCheck(board, siblings=self._boards)
            for board in self._boards
        )
        self._checks_by_id = {id(board): check for board, check in zip(self._boards, self._checks)}

    def ok(self, _message: str = "") -> None:
        """Allow smoke tests that only need to run the stage."""
        return None

    def names(self) -> tuple[str, ...]:
        """Return deterministic board names in stage return order."""
        return tuple(model_output_name(board, self._boards) for board in self._boards)

    def board(self, name: str | None = None) -> BoardCheck:
        """Return one board check by name, or the only board."""
        board = one_or_named_projection(self._boards, name, "board", "Project stage test")
        return self._checks_by_id[id(board)]

    def boards(self) -> tuple[BoardCheck, ...]:
        """Return board checks in deterministic stage return order."""
        return self._checks

    def has_outline(self) -> None:
        """Assert the only board has a non-empty mechanical outline."""
        self.board().has_outline()

    def places(self, *references: str) -> None:
        """Assert the only board places every requested component."""
        self.board().places(*references)


def _component_id_by_reference(design: Design, reference: str) -> str:
    try:
        component = design.component(reference)
    except KeyError as error:
        raise AssertionError(f"Design {design.name} has no component {reference}") from error
    return f"component:{component.index}"
