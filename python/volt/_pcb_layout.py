"""Schematic-style PCB placement authoring helpers."""

from __future__ import annotations

from contextlib import contextmanager
from math import atan2, ceil, cos, degrees, floor, radians, sin
from typing import TYPE_CHECKING

from ._pcb_composition import (
    BoardFanout,
    BoardLayoutComposition,
    _direction,
    _direction_offset,
    align as _compose_align,
    bundle as _compose_bundle,
    connect as _compose_connect,
    distribute as _compose_distribute,
    fanout as _compose_fanout,
    keepout as _compose_keepout,
    mirror as _compose_mirror,
    polygon as _compose_polygon,
    rect as _compose_rect,
    rule as _compose_rule,
    stitch as _compose_stitch,
    text as _compose_text,
    zone as _compose_zone,
)
from . import _volt
from ._utils import _coordinate, _positive_coordinate
from .logical import Component, Net, Pin, _pin_refs_by_name

if TYPE_CHECKING:
    from .pcb import Board, ComponentFootprintPad


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
        pad: int,
        pad_label: str,
        pin: Pin | None,
        status: str,
    ):
        super().__init__(point, board=board)
        self.placement = placement
        self.component = component
        self.pad = pad
        self.pad_label = pad_label
        self.pin = pin
        self.status = status
        self._route_identity = board.route_pad(placement, pad)

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
                    pad=resolution.pad,
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
        grid: float | None = None,
        coordinate_origin: tuple[float, float] = (0, 0),
    ):
        self._board = board
        self._coordinate_origin = _board_point_tuple(coordinate_origin)
        self._grid = (
            None if grid is None else _positive_coordinate(grid, "Board layout grid")
        )
        self._here = self._anchor_at(at)
        self._direction = _direction(direction)
        self._unit = _positive_coordinate(unit, "Board layout unit")

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

    @property
    def grid(self) -> float | None:
        """Return the placement grid spacing, if explicit coordinates are snapped."""
        return self._grid

    def snap(self, point: tuple[float, float] | BoardAnchor | None = None) -> BoardAnchor:
        """Return an anchor snapped to the layout grid."""
        anchor = self._here if point is None else self._anchor_at(point)
        return self._snap_anchor(anchor)

    def snap_x(self, anchor_or_x) -> float:
        """Return an x coordinate snapped to the layout grid."""
        return self._snap_coordinate(
            _board_anchor_axis_target(anchor_or_x, self._board, "x")
        )

    def snap_y(self, anchor_or_y) -> float:
        """Return a y coordinate snapped to the layout grid."""
        return self._snap_coordinate(
            _board_anchor_axis_target(anchor_or_y, self._board, "y")
        )

    def rule(self, name: str) -> float:
        """Return a board design-rule value by a compact authoring name."""
        return _compose_rule(self._composition(), name)

    def align(
        self,
        anchors,
        *,
        axis: str,
        target: tuple[float, float] | BoardAnchor | float | None = None,
    ) -> tuple[BoardAnchor, ...]:
        """Return anchors aligned along one board axis."""
        return _compose_align(self._composition(), anchors, axis=axis, target=target)

    def distribute(
        self,
        *,
        count: int,
        start: tuple[float, float] | BoardAnchor,
        end: tuple[float, float] | BoardAnchor,
    ) -> tuple[BoardAnchor, ...]:
        """Return evenly distributed anchors between two endpoints."""
        return _compose_distribute(self._composition(), count=count, start=start, end=end)

    def mirror(
        self,
        anchors,
        *,
        axis: str,
        about: tuple[float, float] | BoardAnchor | float | None = None,
    ) -> tuple[BoardAnchor, ...]:
        """Return anchors mirrored across a board x or y axis."""
        return _compose_mirror(self._composition(), anchors, axis=axis, about=about)

    def move(self, *, dx: float = 0, dy: float = 0) -> BoardLayout:
        """Move the layout cursor by a relative offset."""
        self._here = self._snap_anchor(self._here.offset(dx=dx, dy=dy))
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
        self._here = self._snap_anchor(self._anchor_at(anchor).offset(dx=dx, dy=dy))
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
        return BoardTwoPadComponent(self, component, side=side, locked=locked)

    def route(self, net=None, *, layer, width=None, mode="octilinear",
              policy=_volt.RoutePolicy.STRICT, via_size=None):
        """Describe one native Strict route with inherited sizing by default."""
        return BoardRoute(self, net, layer=layer, width=width, mode=mode,
                          policy=policy, via_size=via_size)

    def connect(self, start, end, *, layer, net=None, width=None, through=(),
                mode="octilinear", policy=_volt.RoutePolicy.STRICT):
        """Connect anchors through the native route planner, returning RouteResult."""
        return _compose_connect(self._composition(), start, end, layer=layer, net=net,
                                width=width, through=through, mode=mode, policy=policy)

    def bundle(self, pairs, *, layer, net=None, width=None, mode="octilinear",
               policy=_volt.RoutePolicy.STRICT):
        """Publish independently atomic routes in deterministic pair order."""
        return _compose_bundle(self._composition(), pairs, layer=layer, net=net,
                               width=width, mode=mode, policy=policy)

    def via(self, net, *, at=None, start_layer, end_layer, drill=None, annular=None,
            policy=_volt.RoutePolicy.STRICT):
        """Publish a native via-only route; return its owned RouteResult."""
        endpoint = self._here if at is None else at
        return self.route(net, layer=start_layer, policy=policy,
                          via_size=_route_via_size(drill, annular)).at(endpoint).via(end_layer).finish()

    def stitch(self, net, *, at, start_layer, end_layer, drill=None, annular=None,
               policy=_volt.RoutePolicy.STRICT):
        """Publish native via-only routes, each independently atomic."""
        return _compose_stitch(self._composition(), net, at=at, start_layer=start_layer,
                               end_layer=end_layer, drill=drill, annular=annular, policy=policy)

    def fanout(self, anchors, *, layer, direction, distance, net=None, width=None,
               via_layers=None, drill=None, annular=None, policy=_volt.RoutePolicy.STRICT):
        """Author explicit native axial stubs, each with its optional inline via."""
        return _compose_fanout(self._composition(), anchors, layer=layer, direction=direction,
                               distance=distance, net=net, width=width, via_layers=via_layers,
                               drill=drill, annular=annular, policy=policy)

    def node(
        self,
        at: tuple[float, float] | BoardAnchor | None = None,
        *,
        dx: float = 0,
        dy: float = 0,
    ) -> BoardAnchor:
        """Return a reusable board-local geometry anchor without adding objects."""
        base = self._here if at is None else self._anchor_at(at)
        return self._snap_anchor(base.offset(dx=dx, dy=dy))

    def polygon(self, vertices) -> tuple[BoardAnchor, ...]:
        """Return a polygon outline from board anchors or local coordinates."""
        return _compose_polygon(self._composition(), vertices)

    def rect(
        self,
        *,
        at: tuple[float, float] | BoardAnchor | None = None,
        size: tuple[float, float],
    ) -> tuple[BoardAnchor, ...]:
        """Return a rectangular outline from a board anchor and size."""
        return _compose_rect(self._composition(), at=at, size=size)

    rectangle = rect

    def zone(
        self,
        *,
        layers,
        net: Net | int | None = None,
        outline=None,
        at: tuple[float, float] | BoardAnchor | None = None,
        size: tuple[float, float] | None = None,
        fill: str = "solid",
        priority: int = 0,
    ) -> int:
        """Add a copper zone from layout-authored geometry."""
        return _compose_zone(
            self._composition(),
            layers=layers,
            net=net,
            outline=outline,
            at=at,
            size=size,
            fill=fill,
            priority=priority,
        )

    def keepout(
        self,
        *,
        layers,
        restrictions,
        outline=None,
        at: tuple[float, float] | BoardAnchor | None = None,
        size: tuple[float, float] | None = None,
    ) -> int:
        """Add a keepout from layout-authored geometry."""
        return _compose_keepout(
            self._composition(),
            layers=layers,
            restrictions=restrictions,
            outline=outline,
            at=at,
            size=size,
        )

    def text(
        self,
        text: str,
        *,
        at: tuple[float, float] | BoardAnchor,
        layer: int,
        rotation: float = 0.0,
        size: float = 1.0,
        locked: bool = False,
    ) -> int:
        """Add board text at a layout anchor."""
        return _compose_text(
            self._composition(),
            text,
            at=at,
            layer=layer,
            rotation=rotation,
            size=size,
            locked=locked,
        )

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
            anchors.append(self._snap_anchor(base.offset(dx=dx, dy=dy)))
        return tuple(anchors)

    @contextmanager
    def hold(self):
        """Temporarily author from the current cursor, then restore cursor state."""
        saved_here = self._here
        saved_direction = self._direction
        try:
            yield self
        finally:
            self._here = saved_here
            self._direction = saved_direction

    @contextmanager
    def frame(
        self,
        at: tuple[float, float] | BoardAnchor = (0, 0),
        *,
        direction: str | None = None,
    ):
        """Temporarily author in a local board coordinate frame."""
        saved_origin = self._coordinate_origin
        saved_here = self._here
        saved_direction = self._direction
        origin = self._anchor_at(at).point
        self._coordinate_origin = origin
        self._here = BoardAnchor(origin, board=self._board)
        if direction is not None:
            self._direction = _direction(direction)
        try:
            yield self
        finally:
            self._coordinate_origin = saved_origin
            self._here = saved_here
            self._direction = saved_direction

    def __enter__(self) -> BoardLayout:
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        return False

    def _anchor_at(self, value: tuple[float, float] | BoardAnchor) -> BoardAnchor:
        if isinstance(value, BoardAnchor):
            _board_point(value, board=self._board)
            return value
        point = _board_point(value, board=self._board)
        if isinstance(value, (tuple, list)):
            point = (
                point[0] + self._coordinate_origin[0],
                point[1] + self._coordinate_origin[1],
            )
            point = self._snap_point(point)
        return BoardAnchor(point, board=self._board)

    def _snap_anchor(self, anchor: BoardAnchor) -> BoardAnchor:
        return BoardAnchor(self._snap_point(anchor.point), board=self._board)

    def _snap_point(self, point: tuple[float, float]) -> tuple[float, float]:
        if self._grid is None:
            return point
        return (
            self._snap_coordinate(point[0]),
            self._snap_coordinate(point[1]),
        )

    def _snap_coordinate(self, value: float) -> float:
        if self._grid is None:
            return value
        scaled = value / self._grid
        rounded = floor(scaled + 0.5) if scaled >= 0 else ceil(scaled - 0.5)
        return _clean_coordinate(rounded * self._grid)

    def _composition(self) -> BoardLayoutComposition:
        return BoardLayoutComposition(
            board=self._board,
            here=lambda: self.here,
            anchor_at=self._anchor_at,
            snap_anchor=self._snap_anchor,
            axis_target=self._composition_axis_target,
            is_anchor=lambda value: isinstance(value, BoardAnchor),
            hold=self.hold,
            route_for_connect=self._route_for_connect,
            via=self.via,
            connect=self.connect,
        )

    def _route_for_connect(self, net, *, layer, width=None, mode="octilinear",
                           policy=_volt.RoutePolicy.STRICT, via_size=None):
        return self.route(net, layer=layer, width=width, mode=mode, policy=policy,
                          via_size=via_size)

    def _composition_axis_target(self, target, axis: str) -> float:
        coordinate = _board_anchor_axis_target(target, self._board, axis)
        if isinstance(target, BoardAnchor):
            return coordinate
        return self._snap_coordinate(coordinate)

    def __repr__(self) -> str:
        return (
            f"BoardLayout(here={self._here.point!r}, "
            f"direction={self._direction!r}, unit={self._unit!r}, "
            f"grid={self._grid!r})"
        )


class BoardRoute:
    """Fluent description of one native, atomically checked route."""

    def __init__(self, layout, net, *, layer, width=None, mode="octilinear",
                 policy=_volt.RoutePolicy.STRICT, via_size=None):
        self._layout = layout
        self._board = layout._board
        self._net = net
        self._layer = layer
        self._width = width
        self._mode = _route_mode(mode)
        self._policy = policy
        self._via_size = via_size
        self._start = _route_endpoint(layout.here, self._board)
        self._steps = []
        self._committed = False

    def at(self, endpoint):
        """Choose an owner-bearing endpoint or an explicit point as the start."""
        self._require_open("at")
        self._start = _route_endpoint(endpoint, self._board)
        self._steps = []
        return self

    def through(self, endpoint, *, mode=None):
        """Append a native endpoint leg without publishing copper."""
        self._require_open("through")
        self._steps.append(_volt.RouteThrough(_route_endpoint(endpoint, self._board),
                                              self._corner(mode)))
        return self

    def tox(self, endpoint_or_x):
        """Project an endpoint's native X coordinate, or author a literal X."""
        return self._axis(endpoint_or_x, _volt.RouteAxis.X)

    def toy(self, endpoint_or_y):
        """Project an endpoint's native Y coordinate, or author a literal Y."""
        return self._axis(endpoint_or_y, _volt.RouteAxis.Y)

    def _axis(self, target, axis):
        self._require_open("axis")
        if isinstance(target, (int, float)):
            step = _volt.RouteToX(target) if axis == _volt.RouteAxis.X else _volt.RouteToY(target)
        else:
            step = _volt.RouteAlign(_route_endpoint(target, self._board), axis)
        self._steps.append(step)
        return self

    def left(self, distance):
        """Append a native negative-X displacement, without grid snapping."""
        return self._offset(_volt.RouteAxis.X, -distance)

    def right(self, distance):
        """Append a native positive-X displacement, without grid snapping."""
        return self._offset(_volt.RouteAxis.X, distance)

    def up(self, distance):
        """Append a native negative-Y displacement, without grid snapping."""
        return self._offset(_volt.RouteAxis.Y, -distance)

    def down(self, distance):
        """Append a native positive-Y displacement, without grid snapping."""
        return self._offset(_volt.RouteAxis.Y, distance)

    def _offset(self, axis, distance):
        self._require_open("offset")
        self._steps.append(_volt.RouteOffset(axis, distance))
        return self

    def via(self, layer):
        """Switch layers with a native via at the current native point."""
        self._require_open("via")
        self._steps.append(_volt.RouteVia(self._board.route_layer(layer)))
        return self

    def exit(self, operand, direction, distance=0.0):
        """Append a native Exit target."""
        return self._symbolic(_volt.RouteExit(_route_operand(operand, self._board), direction, distance))

    def beside(self, operand, direction, distance=0.0):
        """Append a native Beside target."""
        return self._symbolic(_volt.RouteBeside(_route_operand(operand, self._board), direction, distance))

    def gap(self, lower, upper, axis):
        """Append a native single-route Gap target."""
        return self._symbolic(_volt.RouteGap(_route_operand(lower, self._board), _route_operand(upper, self._board), axis))

    def _symbolic(self, step):
        self._require_open("target")
        self._steps.append(step)
        return self

    def query_gap(self):
        """Read the native gap prefix without closing the builder or moving the cursor."""
        self._require_open("query_gap")
        return self._board.query_gap(self._spec(self._steps))

    def to(self, endpoint, *, mode=None):
        """Check and publish the complete route, returning its owned native result."""
        return self._publish(_volt.RouteTo(_route_endpoint(endpoint, self._board), self._corner(mode)))

    def finish(self):
        """Check and publish at the native current point (including a via-only route)."""
        return self._publish(_volt.RouteFinish())

    def _corner(self, mode):
        name = self._mode if mode is None else _route_mode(mode)
        return {"octilinear": _volt.CornerStyle.CHAMFER45,
                "orthogonal": _volt.CornerStyle.ORTHOGONAL,
                "direct": _volt.CornerStyle.DIRECT}[name]

    def _spec(self, steps):
        return self._board.route_spec(start=self._start, layer=self._board.route_layer(self._layer),
                                     steps=steps,
                                     net=None if self._net is None else self._board.route_net(self._net),
                                     width=self._width, via_size=self._via_size)

    def _publish(self, terminal):
        self._require_open("publish")
        result = self._board.route(self._spec([*self._steps, terminal]), self._policy)
        if result.committed:
            self._committed = True
            self._layout._here = BoardAnchor(result.resolved_steps[-1].end, board=self._board)
        return result

    def _require_open(self, method):
        if self._committed:
            raise ValueError(f"Cannot call {method}() after route is materialized")


def _route_endpoint(value, board):
    if isinstance(value, BoardPadAnchor):
        # Captured full footprint identity survives until native preflight, including reselection.
        return value._route_identity
    if isinstance(value, BoardAnchor):
        if value._board is not board:
            raise ValueError("Board anchor belongs to a different board")
        return _volt.RoutePoint(*value.point)
    if isinstance(value, (tuple, list)):
        return _volt.RoutePoint(*value)
    return value


def _route_operand(value, board):
    return _route_endpoint(value, board) if isinstance(value, BoardPadAnchor) else value


def _route_via_size(drill, annular):
    if (drill is None) != (annular is None):
        raise ValueError("Native route via override requires both drill and annular")
    return None if drill is None else _volt.RouteViaOverride(drill, annular)


class BoardTwoPadComponent:
    """Fluent builder for one two-pad PCB component placement."""

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
        self._at = layout.here
        self._anchor_ref: str | int = "start"
        self._drop_ref: str | int = "end"
        self._direction = layout.direction
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
        return self

    def anchor(self, ref: str | int) -> BoardTwoPadComponent:
        """Choose which local pad or pin anchor is fixed to ``at``."""
        self._require_unplaced("anchor")
        self._anchor_ref = ref
        return self

    def drop(self, ref: str | int) -> BoardTwoPadComponent:
        """Choose which local pad or pin anchor drives the cursor endpoint."""
        self._require_unplaced("drop")
        self._drop_ref = ref
        return self

    def right(self) -> PlacedBoardComponent:
        """Orient the footprint so its end pad is to the right of its start pad."""
        return self._set_direction("Right")

    def left(self) -> PlacedBoardComponent:
        """Orient the footprint so its end pad is to the left of its start pad."""
        return self._set_direction("Left")

    def up(self) -> PlacedBoardComponent:
        """Orient the footprint so its end pad is above its start pad."""
        return self._set_direction("Up")

    def down(self) -> PlacedBoardComponent:
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

    def _set_direction(self, direction: str) -> PlacedBoardComponent:
        self._require_unplaced(direction.lower())
        self._direction = _direction(direction)
        return self._materialize()

    def _materialize(self) -> PlacedBoardComponent:
        if self._placed is not None:
            return self._placed
        center = self._center()
        rotation = self._rotation()
        drop = _transform_local_point(
            self._local_anchor(self._drop_ref),
            rotation=rotation,
            side=self._side,
        )
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
        self._layout._here = BoardAnchor(
            (center[0] + drop[0], center[1] + drop[1]),
            board=self._layout._board,
        )
        self._layout._direction = self._direction
        return self._placed

    def _center(self) -> tuple[float, float]:
        aligned = self._at.point
        transformed = _transform_local_point(
            self._local_anchor(self._anchor_ref),
            rotation=self._rotation(),
            side=self._side,
        )
        return (
            _clean_coordinate(aligned[0] - transformed[0]),
            _clean_coordinate(aligned[1] - transformed[1]),
        )

    def _rotation(self) -> float:
        start, end = self._start_end_pads()
        start_point = _transform_local_point(start.position, rotation=0.0, side=self._side)
        end_point = _transform_local_point(end.position, rotation=0.0, side=self._side)
        current_angle = degrees(
            atan2(end_point[1] - start_point[1], end_point[0] - start_point[0])
        )
        rotation = _rotation_from_direction(self._direction) - current_angle
        return _normalize_rotation(rotation)

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
            for pad in self._component_pads():
                if pad.pad_label == ref:
                    return pad.position
        if isinstance(ref, (str, int)):
            pin = self._component[ref]
            pads = self._pads_for_pin(pin.index)
            if len(pads) == 1:
                return pads[0].position
            if len(pads) > 1:
                raise ValueError(
                    f"Board two-pad component {self._component.reference} pin "
                    f"{ref!r} maps to multiple pads; use pad(label)"
                )
        raise ValueError(f"Board two-pad component has no anchor named {ref!r}")

    def _start_end_pads(self) -> tuple[ComponentFootprintPad, ComponentFootprintPad]:
        pins = tuple(self._component._pin_refs())
        start_pads = self._pads_for_pin(pins[0]["index"])
        end_pads = self._pads_for_pin(pins[-1]["index"])
        if not start_pads or not end_pads:
            raise ValueError(
                f"Board two-pad component {self._component.reference} requires selected "
                "physical part pin-pad mappings"
            )
        if len(start_pads) != 1 or len(end_pads) != 1:
            raise ValueError(
                f"Board two-pad component {self._component.reference} requires exactly "
                "one pad mapped to each component pin"
            )
        start = start_pads[0]
        end = end_pads[0]
        if start.position == end.position:
            raise ValueError(
                f"Board two-pad component {self._component.reference} pad anchors "
                "must be distinct"
            )
        return start, end

    def _pad_by_label(self, label: str) -> ComponentFootprintPad:
        matches = tuple(pad for pad in self._component_pads() if pad.pad_label == label)
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

    def _pads_for_pin(self, pin_index: int) -> tuple[ComponentFootprintPad, ...]:
        return tuple(pad for pad in self._component_pads() if pad.pin == pin_index)

    def _component_pads(self) -> tuple[ComponentFootprintPad, ...]:
        pads = self._layout._board._component_footprint_pads(self._component.index)
        if not pads:
            raise ValueError(
                f"Board two-pad component {self._component.reference} requires a "
                "selected physical part with resolved footprint pad geometry"
            )
        return pads

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


def _route_mode(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Board route mode must be a string")
    normalized = value.casefold().replace("-", "_")
    if normalized in {"octilinear", "octi"}:
        return "octilinear"
    if normalized in {"orthogonal", "ortho"}:
        return "orthogonal"
    if normalized == "direct":
        return "direct"
    raise ValueError("Board route mode must be octilinear, orthogonal, or direct")


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


def _rotation_from_direction(direction: str) -> float:
    return {"Right": 0.0, "Down": 90.0, "Left": 180.0, "Up": 270.0}[direction]


def _normalize_rotation(rotation: float) -> float:
    normalized = rotation % 360.0
    if abs(normalized - 360.0) < 1e-12:
        return 0.0
    return _clean_coordinate(normalized)


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
