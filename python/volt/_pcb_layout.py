"""Schematic-style PCB placement authoring helpers."""

from __future__ import annotations

from contextlib import contextmanager
from math import cos, radians, sin
from typing import TYPE_CHECKING

from ._footprint import Footprint
from ._utils import _coordinate, _positive_coordinate
from .logical import Component, Pin, _pin_refs_by_name

if TYPE_CHECKING:
    from .pcb import Board, FootprintPad


class BoardAnchor:
    """Board point that can be offset directionally while preserving author intent."""

    def __init__(self, point: tuple[float, float], *, board: Board):
        self._point = _board_point_tuple(point)
        self._board = board

    @property
    def x(self) -> float:
        """Return the board x coordinate."""
        return self._point[0]

    @property
    def y(self) -> float:
        """Return the board y coordinate."""
        return self._point[1]

    @property
    def point(self) -> tuple[float, float]:
        """Return this anchor as an ``(x, y)`` tuple."""
        return self._point

    def offset(self, dx: float = 0, dy: float = 0) -> BoardAnchor:
        """Return a new anchor offset from this point."""
        return BoardAnchor(
            (self.x + _coordinate(dx), self.y + _coordinate(dy)),
            board=self._board,
        )

    def left(self, distance: float) -> BoardAnchor:
        """Return a new anchor moved left by a distance."""
        return self.offset(dx=-_coordinate(distance))

    def right(self, distance: float) -> BoardAnchor:
        """Return a new anchor moved right by a distance."""
        return self.offset(dx=_coordinate(distance))

    def up(self, distance: float) -> BoardAnchor:
        """Return a new anchor moved up by a distance."""
        return self.offset(dy=-_coordinate(distance))

    def down(self, distance: float) -> BoardAnchor:
        """Return a new anchor moved down by a distance."""
        return self.offset(dy=_coordinate(distance))

    def tox(self, anchor_or_x) -> BoardAnchor:
        """Return an anchor at this y coordinate and the target x coordinate."""
        return BoardAnchor(
            (_board_anchor_axis_target(anchor_or_x, self._board, "x"), self.y),
            board=self._board,
        )

    def toy(self, anchor_or_y) -> BoardAnchor:
        """Return an anchor at this x coordinate and the target y coordinate."""
        return BoardAnchor(
            (self.x, _board_anchor_axis_target(anchor_or_y, self._board, "y")),
            board=self._board,
        )

    def __iter__(self):
        """Iterate over the ``(x, y)`` coordinate values."""
        return iter(self._point)

    def __repr__(self) -> str:
        return f"BoardAnchor(point={self._point!r})"


class BoardPadAnchor(BoardAnchor):
    """Anchor for one resolved footprint pad on a placed component."""

    def __init__(
        self,
        point: tuple[float, float],
        *,
        board: Board,
        placement: int,
        component: Component,
        pad_label: str,
        pin: Pin | None,
        status: str,
    ):
        super().__init__(point, board=board)
        self.placement = placement
        self.component = component
        self.pad_label = pad_label
        self.pin = pin
        self.status = status

    def __repr__(self) -> str:
        return (
            f"BoardPadAnchor(component={self.component.reference!r}, "
            f"pad_label={self.pad_label!r}, point={self.point!r})"
        )


class PlacedBoardComponent:
    """Read-only handle to one placed PCB component footprint."""

    def __init__(
        self,
        board: Board,
        index: int,
        component: Component,
        *,
        at: tuple[float, float],
        rotation: float,
        side: str,
    ):
        self._board = board
        self._index = index
        self._component = component
        self._at = _board_point_tuple(at)
        self._rotation = float(rotation)
        self._side = side

    @property
    def index(self) -> int:
        """Return the kernel placement index."""
        return self._index

    @property
    def component(self) -> Component:
        """Return the logical component represented by this placement."""
        return self._component

    @property
    def rotation(self) -> float:
        """Return the resolved board rotation in degrees."""
        return self._rotation

    @property
    def side(self) -> str:
        """Return the board side for this placement."""
        return self._side

    @property
    def center(self) -> BoardAnchor:
        """Return the placement center anchor."""
        return BoardAnchor(self._at, board=self._board)

    @property
    def start(self) -> BoardPadAnchor:
        """Return the first logical pin's pad anchor for a two-pin component."""
        return self._two_pin_anchor(0, "start")

    @property
    def end(self) -> BoardPadAnchor:
        """Return the last logical pin's pad anchor for a two-pin component."""
        return self._two_pin_anchor(-1, "end")

    def pad(self, label: str) -> BoardPadAnchor:
        """Return a resolved pad anchor by footprint pad label."""
        if not isinstance(label, str):
            raise TypeError("Board pads are addressed by string labels")
        matches = tuple(item for item in self._pad_anchors() if item.pad_label == label)
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            raise ValueError(
                f"Placed board component {self._component.reference} pad {label!r} "
                "is ambiguous"
            )
        raise ValueError(
            f"Placed board component {self._component.reference} has no resolved "
            f"pad {label!r}"
        )

    def pin(self, key: int | str) -> BoardPadAnchor:
        """Return a resolved pad anchor by logical pin number or name."""
        pin = self._component[key]
        matches = tuple(
            item
            for item in self._pad_anchors()
            if item.pin is not None and item.pin.index == pin.index
        )
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            raise ValueError(
                f"Placed board component {self._component.reference} pin {key!r} "
                "maps to multiple pads; use pad(label)"
            )
        raise ValueError(
            f"Placed board component {self._component.reference} has no resolved "
            f"pad for pin {key!r}"
        )

    def pins(self, name: str) -> tuple[BoardPadAnchor, ...]:
        """Return every resolved pad anchor for logical pins with the given name."""
        if not isinstance(name, str):
            raise TypeError("Board component pin groups are addressed by str name")
        matches = _pin_refs_by_name(self._component._pin_refs(), name)
        if not matches:
            raise IndexError("Component has no pin with that name")
        anchors = []
        for item in matches:
            anchors.append(self._pin_index_anchor(item["index"], item["name"]))
        return tuple(anchors)

    def pin_anchors(self) -> tuple[BoardPadAnchor, ...]:
        """Return resolved pad anchors for every logical pin with a single mapped pad."""
        anchors = []
        for item in self._component._pin_refs():
            anchors.append(self._pin_index_anchor(item["index"], item["name"]))
        return tuple(anchors)

    def __getitem__(self, key: int | str) -> BoardPadAnchor:
        """Return a placed component pin anchor by pin name or number."""
        return self.pin(key)

    def __getattr__(self, name: str) -> BoardPadAnchor:
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        if not name.isidentifier():
            raise AttributeError(
                f"Placed board component pin name {name!r} is not a valid Python "
                "attribute; use bracket access"
            )
        matches = _pin_refs_by_name(self._component._pin_refs(), name)
        if not matches:
            raise AttributeError(f"Placed board component has no anchor named {name!r}")
        if len(matches) > 1:
            numbers = ", ".join(f"{item['number']!r}" for item in matches)
            raise AttributeError(
                f"Placed board component pin name {name!r} is ambiguous for "
                f"component {self._component.reference}; use bracket access by pin "
                f"number. Matching pin numbers: {numbers}"
            )
        return self._pin_index_anchor(matches[0]["index"], name)

    def _two_pin_anchor(self, index: int, label: str) -> BoardPadAnchor:
        pins = tuple(self._component._pin_refs())
        if len(pins) != 2:
            raise ValueError(
                f"Placed board component {label} requires exactly two component pins"
            )
        pin_ref = pins[index]
        return self._pin_index_anchor(pin_ref["index"], pin_ref["name"])

    def _pin_index_anchor(self, pin_index: int, label: str) -> BoardPadAnchor:
        matches = tuple(
            item
            for item in self._pad_anchors()
            if item.pin is not None and item.pin.index == pin_index
        )
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            raise ValueError(
                f"Placed board component {self._component.reference} pin {label!r} "
                "maps to multiple pads; use pad(label)"
            )
        raise ValueError(
            f"Placed board component {self._component.reference} has no resolved "
            f"pad for pin {label!r}"
        )

    def _pad_anchors(self) -> tuple[BoardPadAnchor, ...]:
        anchors = []
        for resolution in self._board.resolve_pads():
            if resolution.placement != self._index:
                continue
            pin = (
                None
                if resolution.pin is None
                else Pin(self._component._design, resolution.pin)
            )
            anchors.append(
                BoardPadAnchor(
                    resolution.position,
                    board=self._board,
                    placement=self._index,
                    component=self._component,
                    pad_label=resolution.pad_label,
                    pin=pin,
                    status=resolution.status,
                )
            )
        if not anchors:
            raise ValueError(
                f"Placed board component {self._component.reference} requires a "
                "resolved footprint with pads"
            )
        return tuple(anchors)

    def __dir__(self) -> list[str]:
        result = set(super().__dir__())
        names = [item["name"] for item in self._component._pin_refs()]
        result.update(
            name for name in names if name.isidentifier() and names.count(name) == 1
        )
        return sorted(result)

    def __repr__(self) -> str:
        return f"PlacedBoardComponent(index={self._index}, component={self._component!r})"


class BoardEdge:
    """Mechanical edge helper for a board outline bounding box."""

    def __init__(self, board: Board, name: str):
        self._board = board
        self._name = _edge_name(name)

    def center(self) -> BoardAnchor:
        """Return the center anchor on this board edge."""
        min_x, min_y, max_x, max_y = self._board._outline_bbox()
        if self._name == "left":
            return BoardAnchor((min_x, (min_y + max_y) / 2), board=self._board)
        if self._name == "right":
            return BoardAnchor((max_x, (min_y + max_y) / 2), board=self._board)
        if self._name == "top":
            return BoardAnchor(((min_x + max_x) / 2, min_y), board=self._board)
        return BoardAnchor(((min_x + max_x) / 2, max_y), board=self._board)

    def __repr__(self) -> str:
        return f"BoardEdge(name={self._name!r})"


class BoardLayout:
    """Cursor state for schematic-style PCB placement authoring."""

    def __init__(
        self,
        board: Board,
        *,
        at: tuple[float, float] | BoardAnchor = (0, 0),
        direction: str = "Right",
        unit: float = 1.0,
        coordinate_origin: tuple[float, float] = (0, 0),
    ):
        self._board = board
        self._coordinate_origin = _board_point_tuple(coordinate_origin)
        self._here = self._anchor_at(at)
        self._direction = _direction(direction)
        self._unit = _positive_coordinate(unit, "Board layout unit")
        self._pending: BoardTwoPadComponent | None = None

    @property
    def here(self) -> BoardAnchor:
        """Return the layout cursor's current anchor."""
        return self._here

    @property
    def direction(self) -> str:
        """Return the layout cursor's current direction."""
        return self._direction

    @property
    def unit(self) -> float:
        """Return the layout unit used by stack spacing defaults."""
        return self._unit

    def move(self, *, dx: float = 0, dy: float = 0) -> BoardLayout:
        """Move the layout cursor by a relative offset."""
        self._flush_pending()
        self._here = self._here.offset(dx=dx, dy=dy)
        return self

    def move_from(
        self,
        anchor: tuple[float, float] | BoardAnchor,
        *,
        dx: float = 0,
        dy: float = 0,
        direction: str | None = None,
    ) -> BoardLayout:
        """Move the layout cursor to an anchor plus an optional relative offset."""
        self._flush_pending()
        self._here = self._anchor_at(anchor).offset(dx=dx, dy=dy)
        if direction is not None:
            self._direction = _direction(direction)
        return self

    def place(
        self,
        component: Component,
        *,
        at: tuple[float, float] | BoardAnchor | None = None,
        orient: str | None = None,
        side: str = "top",
        locked: bool = False,
    ) -> PlacedBoardComponent:
        """Place a component at the cursor or an explicit board anchor."""
        self._flush_pending()
        if not isinstance(component, Component):
            raise TypeError("Board layout place expects a Component handle")
        if component._design is not self._board._design:
            raise ValueError("Component belongs to a different design")
        center = self._here if at is None else self._anchor_at(at)
        direction = self._direction if orient is None else _direction(orient)
        rotation = _rotation_from_direction(direction)
        placement = self._board.place(
            component,
            at=center.point,
            rotation=rotation,
            side=side,
            locked=locked,
        )
        return PlacedBoardComponent(
            self._board,
            placement,
            component,
            at=center.point,
            rotation=rotation,
            side=side,
        )

    def two_pad(
        self,
        component: Component,
        *,
        side: str = "top",
        locked: bool = False,
    ) -> BoardTwoPadComponent:
        """Start fluent placement for a two-pad component footprint."""
        self._flush_pending()
        element = BoardTwoPadComponent(self, component, side=side, locked=locked)
        self._pending = element
        return element

    def node(
        self,
        at: tuple[float, float] | BoardAnchor | None = None,
        *,
        dx: float = 0,
        dy: float = 0,
    ) -> BoardAnchor:
        """Return a reusable board-local geometry anchor without adding objects."""
        base = self._here if at is None else self._anchor_at(at)
        return base.offset(dx=dx, dy=dy)

    def stack(
        self,
        *,
        count: int,
        direction: str | None = None,
        pitch: float | None = None,
        at: tuple[float, float] | BoardAnchor | None = None,
    ) -> tuple[BoardAnchor, ...]:
        """Return evenly spaced anchors from the current cursor or an explicit anchor."""
        if isinstance(count, bool) or not isinstance(count, int):
            raise TypeError("Board layout stack count must be an integer")
        if count < 0:
            raise ValueError("Board layout stack count must not be negative")
        stack_direction = self._direction if direction is None else _direction(direction)
        stack_pitch = self._unit if pitch is None else _positive_coordinate(
            pitch, "Board layout stack pitches"
        )
        base = self._here if at is None else self._anchor_at(at)
        anchors = []
        for index in range(count):
            dx, dy = _direction_offset(stack_direction, stack_pitch * index)
            anchors.append(base.offset(dx=dx, dy=dy))
        return tuple(anchors)

    @contextmanager
    def hold(self):
        """Temporarily author from the current cursor, then restore cursor state."""
        self._flush_pending()
        saved_here = self._here
        saved_direction = self._direction
        saved_pending = self._pending
        try:
            yield self
            self._flush_pending()
        finally:
            self._here = saved_here
            self._direction = saved_direction
            self._pending = saved_pending

    @contextmanager
    def frame(
        self,
        at: tuple[float, float] | BoardAnchor = (0, 0),
        *,
        direction: str | None = None,
    ):
        """Temporarily author in a local board coordinate frame."""
        self._flush_pending()
        saved_origin = self._coordinate_origin
        saved_here = self._here
        saved_direction = self._direction
        saved_pending = self._pending
        origin = self._anchor_at(at).point
        self._coordinate_origin = origin
        self._here = BoardAnchor(origin, board=self._board)
        if direction is not None:
            self._direction = _direction(direction)
        self._pending = None
        try:
            yield self
            self._flush_pending()
        finally:
            self._coordinate_origin = saved_origin
            self._here = saved_here
            self._direction = saved_direction
            self._pending = saved_pending

    def __enter__(self) -> BoardLayout:
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        if exc_type is None:
            self._flush_pending()
        return False

    def _anchor_at(self, value: tuple[float, float] | BoardAnchor) -> BoardAnchor:
        point = _board_point(value, board=self._board)
        if isinstance(value, (tuple, list)):
            point = (
                point[0] + self._coordinate_origin[0],
                point[1] + self._coordinate_origin[1],
            )
        return BoardAnchor(point, board=self._board)

    def _flush_pending(self) -> None:
        pending = self._pending
        if pending is not None:
            pending._materialize()
            if self._pending is pending:
                self._pending = None

    def __repr__(self) -> str:
        return (
            f"BoardLayout(here={self._here.point!r}, "
            f"direction={self._direction!r}, unit={self._unit!r})"
        )


class BoardTwoPadComponent:
    """Deferred fluent placement for one two-pad PCB component."""

    def __init__(
        self,
        layout: BoardLayout,
        component: Component,
        *,
        side: str = "top",
        locked: bool = False,
    ):
        if not isinstance(component, Component):
            raise TypeError("Board two-pad placement expects a Component handle")
        if component._design is not layout._board._design:
            raise ValueError("Component belongs to a different design")
        pins = tuple(component._pin_refs())
        if len(pins) != 2:
            raise ValueError("Board two-pad placement requires exactly two component pins")
        self._layout = layout
        self._component = component
        self._side = side
        self._locked = locked
        self._start_here = layout.here
        self._start_direction = layout.direction
        self._at = layout.here
        self._anchor_ref: str | int = "start"
        self._drop_ref: str | int = "end"
        self._direction = layout.direction
        self._cursor_committed = False
        self._placed: PlacedBoardComponent | None = None

    @property
    def index(self) -> int:
        """Return the kernel placement index after materialization."""
        return self._materialize().index

    @property
    def component(self) -> Component:
        """Return the component being placed."""
        return self._component

    @property
    def center(self) -> BoardAnchor:
        """Return the placed footprint center after materialization."""
        return self._materialize().center

    @property
    def start(self) -> BoardPadAnchor:
        """Return the start pad anchor after materialization."""
        return self._materialize().start

    @property
    def end(self) -> BoardPadAnchor:
        """Return the end pad anchor after materialization."""
        return self._materialize().end

    def at(self, point: tuple[float, float] | BoardAnchor) -> BoardTwoPadComponent:
        """Set the anchor point for the two-pad placement."""
        self._require_unplaced("at")
        self._at = self._layout._anchor_at(point)
        self._commit_cursor()
        return self

    def anchor(self, ref: str | int) -> BoardTwoPadComponent:
        """Choose which local pad or pin anchor is fixed to ``at``."""
        self._require_unplaced("anchor")
        self._anchor_ref = ref
        self._commit_cursor()
        return self

    def drop(self, ref: str | int) -> BoardTwoPadComponent:
        """Choose which local pad or pin anchor drives the cursor endpoint."""
        self._require_unplaced("drop")
        self._drop_ref = ref
        self._commit_cursor()
        return self

    def right(self) -> BoardTwoPadComponent:
        """Orient the footprint so its end pad is to the right of its start pad."""
        return self._set_direction("Right")

    def left(self) -> BoardTwoPadComponent:
        """Orient the footprint so its end pad is to the left of its start pad."""
        return self._set_direction("Left")

    def up(self) -> BoardTwoPadComponent:
        """Orient the footprint so its end pad is above its start pad."""
        return self._set_direction("Up")

    def down(self) -> BoardTwoPadComponent:
        """Orient the footprint so its end pad is below its start pad."""
        return self._set_direction("Down")

    def pad(self, label: str) -> BoardPadAnchor:
        """Materialize and return a pad anchor by footprint pad label."""
        return self._materialize().pad(label)

    def pin(self, key: int | str) -> BoardPadAnchor:
        """Materialize and return a pin anchor by logical pin number or name."""
        return self._materialize().pin(key)

    def __getitem__(self, key: int | str) -> BoardPadAnchor:
        """Materialize and return a pin anchor by logical pin number or name."""
        return self._materialize()[key]

    def __getattr__(self, name: str) -> BoardPadAnchor:
        return getattr(self._materialize(), name)

    def _set_direction(self, direction: str) -> BoardTwoPadComponent:
        self._require_unplaced(direction.lower())
        self._direction = _direction(direction)
        self._commit_cursor()
        return self

    def _commit_cursor(self) -> None:
        try:
            center = self._center()
            drop = _transform_local_point(
                self._local_anchor(self._drop_ref),
                rotation=_rotation_from_direction(self._direction),
                side=self._side,
            )
            self._layout._here = BoardAnchor(
                (center[0] + drop[0], center[1] + drop[1]),
                board=self._layout._board,
            )
            self._layout._direction = self._direction
            self._cursor_committed = True
        except Exception:
            if self._layout._pending is self:
                self._layout._pending = None
            self._restore_layout_state()
            raise

    def _materialize(self) -> PlacedBoardComponent:
        if self._placed is not None:
            return self._placed
        try:
            center = self._center()
            rotation = _rotation_from_direction(self._direction)
            placement = self._layout._board.place(
                self._component,
                at=center,
                rotation=rotation,
                side=self._side,
                locked=self._locked,
            )
            self._placed = PlacedBoardComponent(
                self._layout._board,
                placement,
                self._component,
                at=center,
                rotation=rotation,
                side=self._side,
            )
            if not self._cursor_committed:
                self._commit_cursor()
            if self._layout._pending is self:
                self._layout._pending = None
            return self._placed
        except Exception:
            if self._layout._pending is self:
                self._layout._pending = None
            self._restore_layout_state()
            raise

    def _restore_layout_state(self) -> None:
        if self._cursor_committed:
            self._layout._here = self._start_here
            self._layout._direction = self._start_direction
            self._cursor_committed = False

    def _center(self) -> tuple[float, float]:
        aligned = self._at.point
        transformed = _transform_local_point(
            self._local_anchor(self._anchor_ref),
            rotation=_rotation_from_direction(self._direction),
            side=self._side,
        )
        return (
            _clean_coordinate(aligned[0] - transformed[0]),
            _clean_coordinate(aligned[1] - transformed[1]),
        )

    def _local_anchor(self, ref: str | int) -> tuple[float, float]:
        start_pad, end_pad = self._start_end_pads()
        if isinstance(ref, str):
            normalized = ref.casefold()
            if normalized == "start":
                return start_pad.position
            if normalized == "end":
                return end_pad.position
            if normalized == "center":
                return (
                    (start_pad.position[0] + end_pad.position[0]) / 2,
                    (start_pad.position[1] + end_pad.position[1]) / 2,
                )
            for pad in self._footprint().pads:
                if pad.label == ref:
                    return pad.position
        if isinstance(ref, (str, int)):
            pin = self._component[ref]
            labels = self._pin_pad_labels().get(pin.index, ())
            if len(labels) == 1:
                return self._pad_by_label(labels[0]).position
            if len(labels) > 1:
                raise ValueError(
                    f"Board two-pad component {self._component.reference} pin "
                    f"{ref!r} maps to multiple pads; use pad(label)"
                )
        raise ValueError(f"Board two-pad component has no anchor named {ref!r}")

    def _start_end_pads(self) -> tuple[FootprintPad, FootprintPad]:
        pins = tuple(self._component._pin_refs())
        labels = self._pin_pad_labels()
        try:
            start_labels = labels[pins[0]["index"]]
            end_labels = labels[pins[-1]["index"]]
        except KeyError as error:
            raise ValueError(
                f"Board two-pad component {self._component.reference} requires selected "
                "physical part pin-pad mappings"
            ) from error
        if len(start_labels) != 1 or len(end_labels) != 1:
            raise ValueError(
                f"Board two-pad component {self._component.reference} requires exactly "
                "one pad mapped to each component pin"
            )
        start = self._pad_by_label(start_labels[0])
        end = self._pad_by_label(end_labels[0])
        if start.position == end.position:
            raise ValueError(
                f"Board two-pad component {self._component.reference} pad anchors "
                "must be distinct"
            )
        return start, end

    def _pad_by_label(self, label: str) -> FootprintPad:
        matches = tuple(pad for pad in self._footprint().pads if pad.label == label)
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            raise ValueError(
                f"Board-ready footprint for {self._component.reference} has ambiguous "
                f"pad label {label!r}"
            )
        raise ValueError(
            f"Board-ready footprint for {self._component.reference} has no pad "
            f"label {label!r}"
        )

    def _footprint(self) -> Footprint:
        footprint = self._layout._board._design._object_footprint_for_component(
            self._component.index
        )
        if footprint is None:
            raise ValueError(
                f"Board two-pad component {self._component.reference} requires a "
                "board-ready footprint with pad geometry"
            )
        return footprint

    def _pin_pad_labels(self) -> dict[int, tuple[str, ...]]:
        return self._component._design._component_pin_pad_mappings.get(
            self._component.index,
            {},
        )

    def _require_unplaced(self, method: str) -> None:
        if self._placed is not None:
            raise ValueError(
                f"Cannot call {method}() after two-pad placement is materialized"
            )

    def __repr__(self) -> str:
        return (
            f"BoardTwoPadComponent(component={self._component!r}, "
            f"direction={self._direction!r})"
        )


def _board_point_tuple(value) -> tuple[float, float]:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise TypeError("Board points must be anchors or (x, y) pairs")
    return (_coordinate(value[0]), _coordinate(value[1]))


def _board_point(value, *, board: Board) -> tuple[float, float]:
    if isinstance(value, BoardAnchor):
        if value._board is not board:
            raise ValueError("Board anchor belongs to a different board")
        return value.point
    return _board_point_tuple(value)


def _board_anchor_axis_target(target, board: Board, axis: str) -> float:
    if isinstance(target, BoardAnchor):
        if target._board is not board:
            raise ValueError("Board anchor belongs to a different board")
        return target.x if axis == "x" else target.y
    return _coordinate(target)


def _direction(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Board layout direction must be a string")
    normalized = {
        "right": "Right",
        "down": "Down",
        "left": "Left",
        "up": "Up",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError("Board layout direction must be Right, Down, Left, or Up")
    return normalized


def _rotation_from_direction(direction: str) -> float:
    return {"Right": 0.0, "Down": 90.0, "Left": 180.0, "Up": 270.0}[direction]


def _direction_offset(direction: str, distance: float) -> tuple[float, float]:
    if direction == "Right":
        return (distance, 0.0)
    if direction == "Left":
        return (-distance, 0.0)
    if direction == "Down":
        return (0.0, distance)
    return (0.0, -distance)


def _transform_local_point(
    point: tuple[float, float],
    *,
    rotation: float,
    side: str,
) -> tuple[float, float]:
    x, y = point
    if side == "bottom":
        x = -x
    angle = radians(rotation)
    result = (x * cos(angle) - y * sin(angle), x * sin(angle) + y * cos(angle))
    return (_clean_coordinate(result[0]), _clean_coordinate(result[1]))


def _clean_coordinate(value: float) -> float:
    if abs(value) < 1e-12:
        return 0.0
    return value


def _edge_name(name: str) -> str:
    if not isinstance(name, str):
        raise TypeError("Board edge name must be a string")
    normalized = name.casefold()
    if normalized not in {"left", "right", "top", "bottom"}:
        raise ValueError("Board edge name must be left, right, top, or bottom")
    return normalized


def corner_point(board: Board, name: str) -> BoardAnchor:
    """Return a board outline corner anchor."""
    if not isinstance(name, str):
        raise TypeError("Board corner name must be a string")
    normalized = name.casefold().replace("_", "-").replace(" ", "-")
    aliases = {
        "top-left": "top-left",
        "left-top": "top-left",
        "tl": "top-left",
        "top-right": "top-right",
        "right-top": "top-right",
        "tr": "top-right",
        "bottom-left": "bottom-left",
        "left-bottom": "bottom-left",
        "bl": "bottom-left",
        "bottom-right": "bottom-right",
        "right-bottom": "bottom-right",
        "br": "bottom-right",
    }
    corner = aliases.get(normalized)
    if corner is None:
        raise ValueError(
            "Board corner name must be top-left, top-right, bottom-left, or bottom-right"
        )
    min_x, min_y, max_x, max_y = board._outline_bbox()
    return {
        "top-left": BoardAnchor((min_x, min_y), board=board),
        "top-right": BoardAnchor((max_x, min_y), board=board),
        "bottom-left": BoardAnchor((min_x, max_y), board=board),
        "bottom-right": BoardAnchor((max_x, max_y), board=board),
    }[corner]


def center_point(board: Board) -> BoardAnchor:
    """Return the center anchor of the board outline bounding box."""
    min_x, min_y, max_x, max_y = board._outline_bbox()
    return BoardAnchor(((min_x + max_x) / 2, (min_y + max_y) / 2), board=board)
