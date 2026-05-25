"""Schematic authoring and projection helpers for the Volt Python facade."""

from __future__ import annotations

import sys
from contextlib import contextmanager
from pathlib import Path
from typing import Iterable

from ._schematic_labels import _component_value_label
from ._presentation import (
    _needs_generated_two_terminal_symbol,
    _presentation_pin_matches,
    _presentation_symbol_pins,
    _presentation_symbol_spec,
    _rotate_symbol_point,
    _transform_symbol_point,
)
from ._schematic_metadata import _optional_text_font_size, _schematic_svg_page_filename
from ._schematic_routing import (
    _multi_anchor_orthogonal_wire_points,
    _multi_anchor_shape_wire_points,
    _normalize_schematic_route_points,
    _orthogonal_wire_points,
    _schematic_route_has_distinct_points,
    _shape_wire_points,
)
from ._utils import (
    _coordinate,
    _nonnegative_coordinate,
    _orientation,
    _positive_coordinate,
    _string_dict,
)
from .diagnostics import DiagnosticReport, _diagnostic_from_dict
from .library import (
    SchematicSymbolSpec,
    _default_two_terminal_symbol_spec,
    _text_horizontal_alignment,
    _text_vertical_alignment,
)
from .logical import Component, Net, Pin, ModuleInstancePort, _pin_refs_by_name


class SchematicAnchor:
    """Sheet point that can be offset directionally while preserving author intent."""

    def __init__(self, point: tuple[float, float], *, design: Design | None = None):
        self._point = _schematic_point_tuple(point)
        self._design = design

    @property
    def x(self) -> float:
        return self._point[0]

    @property
    def y(self) -> float:
        return self._point[1]

    @property
    def point(self) -> tuple[float, float]:
        return self._point

    def offset(self, dx: float = 0, dy: float = 0) -> SchematicAnchor:
        return SchematicAnchor(
            (self.x + _coordinate(dx), self.y + _coordinate(dy)),
            design=self._design,
        )

    def left(self, distance: float) -> SchematicAnchor:
        return self.offset(dx=-_coordinate(distance))

    def right(self, distance: float) -> SchematicAnchor:
        return self.offset(dx=_coordinate(distance))

    def up(self, distance: float) -> SchematicAnchor:
        return self.offset(dy=-_coordinate(distance))

    def down(self, distance: float) -> SchematicAnchor:
        return self.offset(dy=_coordinate(distance))

    def tox(self, anchor_or_x) -> SchematicAnchor:
        """Return an anchor at this y coordinate and the target x coordinate."""
        return SchematicAnchor(
            (
                _schematic_anchor_axis_target(anchor_or_x, self._design, "x"),
                self.y,
            ),
            design=self._design,
        )

    def toy(self, anchor_or_y) -> SchematicAnchor:
        """Return an anchor at this x coordinate and the target y coordinate."""
        return SchematicAnchor(
            (
                self.x,
                _schematic_anchor_axis_target(anchor_or_y, self._design, "y"),
            ),
            design=self._design,
        )

    def __iter__(self):
        return iter(self._point)

    def __repr__(self) -> str:
        return f"SchematicAnchor(point={self._point!r})"


class SchematicPinAnchor(SchematicAnchor):
    """Anchor for one placed symbol pin and its kernel-owned logical pin."""

    def __init__(
        self,
        point: tuple[float, float],
        *,
        pin: Pin,
        name: str,
        number: str,
        orientation: str,
    ):
        super().__init__(point, design=pin._design)
        self.pin = pin
        self.name = name
        self.number = number
        self.orientation = orientation

    def __repr__(self) -> str:
        return (
            f"SchematicPinAnchor(name={self.name!r}, number={self.number!r}, "
            f"point={self.point!r})"
        )


class SchematicPort:
    """Handle to a placed terminal marker, sheet, or off-page schematic port."""

    def __init__(
        self,
        schematic: Schematic,
        index: int,
        *,
        net: Net,
        name: str,
        kind: str,
        at: tuple[float, float],
        orientation: str,
    ):
        self._schematic = schematic
        self._index = index
        self.net = net
        self.name = name
        self.kind = kind
        self.orientation = orientation
        self.pin = SchematicAnchor(at, design=net._design)

    @property
    def index(self) -> int:
        return self._index

    def __repr__(self) -> str:
        return (
            f"SchematicPort(name={self.name!r}, kind={self.kind!r}, "
            f"index={self._index})"
        )


class SchematicJunction:
    """Read-only handle to an explicit schematic junction."""

    def __init__(self, schematic: Schematic, index: int):
        self._schematic = schematic
        self._index = index

    @property
    def index(self) -> int:
        return self._index

    def __repr__(self) -> str:
        return f"SchematicJunction(index={self._index})"


class SchematicNoConnect:
    """Read-only handle to a schematic no-connect marker."""

    def __init__(self, schematic: Schematic, index: int, pin: Pin):
        self._schematic = schematic
        self._index = index
        self.pin = pin

    @property
    def index(self) -> int:
        return self._index

    def __repr__(self) -> str:
        return f"SchematicNoConnect(index={self._index})"


class SchematicSymbol:
    """Read-only handle to a placed schematic symbol instance."""

    def __init__(
        self,
        schematic: Schematic,
        index: int,
        component: Component | None = None,
        orientation: str | None = None,
        authored_region: int | None = None,
    ):
        self._schematic = schematic
        self._index = index
        self._component = component
        self._orientation = orientation
        self._authored_region = authored_region

    @property
    def index(self) -> int:
        return self._index

    @property
    def component(self) -> Component:
        if self._component is None:
            raise ValueError("Placed symbol component is not available")
        return self._component

    @property
    def orientation(self) -> str:
        if self._orientation is None:
            return self._schematic._design._circuit.schematic_symbol_orientation(self._index)
        return self._orientation

    def pin_anchor(self, number: int | str) -> tuple[float, float]:
        if not isinstance(number, (int, str)):
            raise TypeError("pin_anchor expects a pin number")
        return self._schematic._design._circuit.schematic_symbol_pin_anchor(
            self._index, str(number)
        )

    def pin(self, key: int | str) -> SchematicPinAnchor:
        try:
            pin_ref = _resolve_schematic_symbol_pin_ref(self._pin_refs(), key)
        except ValueError as error:
            if "ambiguous" in str(error):
                raise ValueError(f"{error} for {self._pin_context()}") from error
            raise
        return self._pin_anchor_for_ref(pin_ref)

    def pins(self, name: str) -> tuple[SchematicPinAnchor, ...]:
        if not isinstance(name, str):
            raise TypeError("Schematic symbol pin groups are addressed by str name")
        matches = tuple(item for item in self._pin_refs() if item["name"] == name)
        if not matches:
            raise IndexError("Schematic symbol has no pin with that name")
        return tuple(self._pin_anchor_for_ref(item) for item in matches)

    def pin_anchors(self) -> tuple[SchematicPinAnchor, ...]:
        return tuple(self._pin_anchor_for_ref(item) for item in self._pin_refs())

    def _pin_refs(self):
        return self._schematic._design._circuit.schematic_symbol_pin_refs(self._index)

    def _pin_context(self) -> str:
        if self._component is None:
            return f"symbol instance {self._index} on {_schematic_sheet_phrase(self._schematic)}"
        return (
            f"component {self._component.reference} on "
            f"{_schematic_sheet_phrase(self._schematic)}"
        )

    def _pin_anchor_for_ref(self, pin_ref) -> SchematicPinAnchor:
        if self._component is None:
            raise ValueError(
                "Schematic pin anchors require the Component handle returned by "
                f"Schematic.place() for {_schematic_sheet_phrase(self._schematic)}"
            )
        pin = Pin(
            self._schematic._design,
            self._schematic._design._circuit.pin_by_number(
                self._component.index, pin_ref["number"]
            ),
        )
        return SchematicPinAnchor(
            pin_ref["anchor"],
            pin=pin,
            name=pin_ref["name"],
            number=pin_ref["number"],
            orientation=pin_ref["orientation"],
        )

    def __repr__(self) -> str:
        return f"SchematicSymbol(index={self._index})"


class PlacedSchematicElement:
    """Drawing-session authoring view over one placed schematic symbol."""

    def __init__(self, symbol: SchematicSymbol):
        if not isinstance(symbol, SchematicSymbol):
            raise TypeError("Placed schematic elements wrap a SchematicSymbol handle")
        self.symbol = symbol

    @property
    def index(self) -> int:
        return self.symbol.index

    @property
    def component(self) -> Component:
        return self.symbol.component

    @property
    def orientation(self) -> str:
        return self.symbol.orientation

    @property
    def start(self) -> SchematicPinAnchor:
        return self._terminal_anchor(0, "start")

    @property
    def end(self) -> SchematicPinAnchor:
        return self._terminal_anchor(-1, "end")

    @property
    def center(self) -> SchematicAnchor:
        anchors = self.pin_anchors()
        if not anchors:
            raise ValueError("Placed schematic element center requires at least one pin anchor")
        xs = tuple(anchor.x for anchor in anchors)
        ys = tuple(anchor.y for anchor in anchors)
        return SchematicAnchor(
            ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2),
            design=self.symbol._schematic._design,
        )

    def __getitem__(self, key: int | str) -> SchematicPinAnchor:
        return self.pin(key)

    def __getattr__(self, name: str) -> SchematicPinAnchor:
        if name.startswith("__") and name.endswith("__"):
            raise AttributeError(name)
        if not name.isidentifier():
            raise AttributeError(
                f"Placed schematic element pin name {name!r} is not a valid Python "
                "attribute; use bracket access"
            )
        matches = _pin_refs_by_name(self.symbol._pin_refs(), name)
        if not matches:
            raise AttributeError(f"Placed schematic element has no anchor named {name!r}")
        if len(matches) > 1:
            numbers = ", ".join(f"{item['number']!r}" for item in matches)
            raise AttributeError(
                f"Placed schematic element pin name {name!r} is ambiguous for "
                f"{self.symbol._pin_context()}; use bracket access by pin number or "
                f"pin(number). Matching pin numbers: {numbers}"
            )
        return self.symbol._pin_anchor_for_ref(matches[0])

    def pin_anchor(self, number: int | str) -> tuple[float, float]:
        return self.symbol.pin_anchor(number)

    def pin(self, key: int | str) -> SchematicPinAnchor:
        return self.symbol.pin(key)

    def pins(self, name: str) -> tuple[SchematicPinAnchor, ...]:
        return self.symbol.pins(name)

    def pin_anchors(self) -> tuple[SchematicPinAnchor, ...]:
        return self.symbol.pin_anchors()

    def label(
        self,
        text: str,
        *,
        loc: str | None = None,
        name: str = "label",
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
        align: str = "middle",
        baseline: str = "baseline",
        font_size: float | None = None,
    ) -> PlacedSchematicElement:
        if not isinstance(text, str):
            raise TypeError("Schematic element labels must be strings")
        if not text:
            raise ValueError("Schematic element labels must not be empty")
        if not isinstance(name, str):
            raise TypeError("Schematic element label field names must be strings")
        if not name:
            raise ValueError("Schematic element label field names must not be empty")
        at = _element_label_point(
            self,
            _element_label_loc(self, name, loc),
            _label_offset(offset, ofst, default=_element_label_offset(name)),
        )
        self.symbol._schematic._add_symbol_field(
            self.symbol,
            name=name,
            value=text,
            at=at,
            orient="Right" if orient is None else orient,
            _authored_region=self.symbol._authored_region,
            align=align,
            baseline=baseline,
            font_size=font_size,
        )
        return self

    def label_ref(
        self,
        *,
        loc: str | None = None,
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
        align: str = "middle",
        baseline: str = "baseline",
        font_size: float | None = None,
    ) -> PlacedSchematicElement:
        return self.label(
            self.component.reference,
            loc=loc,
            name="reference",
            offset=offset,
            ofst=ofst,
            orient=orient,
            align=align,
            baseline=baseline,
            font_size=font_size,
        )

    def label_value(
        self,
        *,
        loc: str | None = None,
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
        align: str = "middle",
        baseline: str = "baseline",
        font_size: float | None = None,
    ) -> PlacedSchematicElement:
        value = _component_value_label(self.component)
        if value is None:
            raise ValueError("Component has no value or electrical property to label")
        return self.label(
            str(value),
            loc=loc,
            name="value",
            offset=offset,
            ofst=ofst,
            orient=orient,
            align=align,
            baseline=baseline,
            font_size=font_size,
        )

    def dot(self, *, net: Net | None = None) -> PlacedSchematicElement:
        _add_schematic_junction_dot(
            self.symbol._schematic,
            self.end,
            net=net,
            _authored_region=self.symbol._authored_region,
            action="element endpoint dot",
        )
        return self

    def idot(self, *, net: Net | None = None) -> PlacedSchematicElement:
        _add_schematic_junction_dot(
            self.symbol._schematic,
            self.start,
            net=net,
            _authored_region=self.symbol._authored_region,
            action="element endpoint dot",
        )
        return self

    def _terminal_anchor(self, index: int, label: str) -> SchematicPinAnchor:
        anchors = self.pin_anchors()
        if len(anchors) < 2:
            raise ValueError(
                f"Placed schematic element {label} requires at least two pin anchors"
            )
        return anchors[index]

    def __dir__(self) -> list[str]:
        result = set(super().__dir__())
        names = [item["name"] for item in self.symbol._pin_refs()]
        result.update(name for name in names if name.isidentifier() and names.count(name) == 1)
        result.update({"dot", "idot"})
        return sorted(result)

    def __repr__(self) -> str:
        return f"PlacedSchematicElement(symbol={self.symbol!r})"


class SchematicWire:
    """Read-only handle to a schematic wire run projection."""

    def __init__(
        self,
        schematic: Schematic,
        index: int,
        *,
        net: Net,
        points: Iterable[tuple[float, float]],
        authored_region: int | None = None,
    ):
        self._schematic = schematic
        self._index = index
        self._net = net
        self._points = tuple(points)
        self._authored_region = authored_region
        self._dot_start = False
        self._dot_end = False

    @property
    def index(self) -> int:
        return self._index

    def dot(self) -> SchematicWire:
        if not self._dot_end:
            _add_schematic_junction_dot(
                self._schematic,
                self._points[-1],
                net=self._net,
                _authored_region=self._authored_region,
                action="wire endpoint dot",
            )
            self._dot_end = True
        return self

    def idot(self) -> SchematicWire:
        if not self._dot_start:
            _add_schematic_junction_dot(
                self._schematic,
                self._points[0],
                net=self._net,
                _authored_region=self._authored_region,
                action="wire endpoint dot",
            )
            self._dot_start = True
        return self

    def __repr__(self) -> str:
        return f"SchematicWire(index={self._index})"


class SchematicNetLabel:
    """Read-only handle to a schematic net label projection."""

    def __init__(self, schematic: Schematic, index: int, orientation: str = "Right"):
        self._schematic = schematic
        self._index = index
        self.orientation = orientation

    @property
    def index(self) -> int:
        return self._index

    def __repr__(self) -> str:
        return f"SchematicNetLabel(index={self._index})"


class SchematicSignalStub:
    """Read-only handle to a compact local signal-stub projection."""

    def __init__(
        self,
        schematic: Schematic,
        *,
        net: Net,
        side: str,
        wire: SchematicWire,
        label: SchematicNetLabel,
        start: tuple[float, float],
        end: tuple[float, float],
        label_position: tuple[float, float],
    ):
        self.net = net
        self.side = side
        self.wire = wire
        self.label = label
        self.start = SchematicAnchor(start, design=schematic._design)
        self.end = SchematicAnchor(end, design=schematic._design)
        self.label_position = SchematicAnchor(label_position, design=schematic._design)

    def __repr__(self) -> str:
        return f"SchematicSignalStub(net={self.net.name!r}, side={self.side!r})"


class SchematicSignalTag:
    """Read-only handle to a compact signal tag attached by a short stub."""

    def __init__(
        self,
        schematic: Schematic,
        *,
        net: Net,
        side: str,
        wire: SchematicWire,
        port: SchematicPort,
        start: tuple[float, float],
        end: tuple[float, float],
    ):
        self.net = net
        self.side = side
        self.wire = wire
        self.port = port
        self.start = SchematicAnchor(start, design=schematic._design)
        self.end = SchematicAnchor(end, design=schematic._design)

    def __repr__(self) -> str:
        return f"SchematicSignalTag(net={self.net.name!r}, side={self.side!r})"


class SchematicTerminalStub:
    """Read-only handle to a short wire ending in a terminal marker projection."""

    def __init__(
        self,
        schematic: Schematic,
        *,
        net: Net,
        side: str,
        wire: SchematicWire,
        port: SchematicPort,
        start: tuple[float, float],
        end: tuple[float, float],
    ):
        self.net = net
        self.side = side
        self.wire = wire
        self.port = port
        self.start = SchematicAnchor(start, design=schematic._design)
        self.end = SchematicAnchor(end, design=schematic._design)

    def __repr__(self) -> str:
        return f"SchematicTerminalStub(net={self.net.name!r}, side={self.side!r})"


class SchematicSymbolField:
    """Read-only handle to a placed schematic symbol field projection."""

    def __init__(
        self,
        schematic: Schematic,
        index: int,
        *,
        symbol: SchematicSymbol,
        name: str,
        value: str,
        at: tuple[float, float],
        orientation: str,
    ):
        self._schematic = schematic
        self._index = index
        self.symbol = symbol
        self.name = name
        self.value = value
        self.position = SchematicAnchor(at, design=schematic._design)
        self.orientation = orientation

    @property
    def index(self) -> int:
        return self._index

    def __repr__(self) -> str:
        return f"SchematicSymbolField(name={self.name!r}, index={self._index})"


class SchematicWireBuilder:
    """Fluent authoring helper for one schematic wire run.

    Start with ``from_()``, append any explicit intermediate anchors with ``via()``,
    append the intended endpoint with ``to()``, then persist the run with ``direct()``
    or ``orthogonal()``. Shape shortcuts are Python authoring sugar that lower to
    ordinary schematic wire points on the same logical net.
    """

    def __init__(
        self,
        schematic: Schematic,
        net: Net,
        *,
        drawing=None,
        authored_region: int | None = None,
    ):
        self._schematic = schematic
        self._net = net
        self._points: list[tuple[float, float]] = []
        self._endpoint_payloads: list[tuple[float, float, int | None, int | None]] = []
        self._drawing = drawing
        self._authored_region = authored_region
        self._start_here = drawing.here if drawing is not None else None
        self._start_direction = drawing.direction if drawing is not None else None
        self._wire: SchematicWire | None = None
        self._dot_start = False
        self._dot_end = False

    def at(self, point) -> SchematicWireBuilder:
        return self.from_(point)

    def endpoints(
        self, start, end, *, shape: str | None = None, k: float | None = None
    ) -> SchematicWireBuilder:
        self.from_(start)
        return self.to(end, shape=shape, k=k)

    def from_(self, point) -> SchematicWireBuilder:
        self._require_unmaterialized()
        converted, endpoint = self._point_and_endpoint_for_authoring(point)
        self._points = [converted]
        self._endpoint_payloads = [endpoint]
        self._update_drawing_cursor(self._points[-1])
        return self

    def via(self, point) -> SchematicWireBuilder:
        """Append an explicit intermediate point that the route should preserve."""
        self._require_unmaterialized()
        self._require_started()
        converted, endpoint = self._point_and_endpoint_for_authoring(point)
        self._append_point(converted)
        self._endpoint_payloads.append(endpoint)
        return self

    def to(self, point, *, shape: str | None = None, k: float | None = None):
        """Append the next route point, normally the terminal endpoint."""
        self._require_unmaterialized()
        self._require_started()
        converted, endpoint = self._point_and_endpoint_for_authoring(point)
        self._append_point(converted)
        self._endpoint_payloads.append(endpoint)
        if shape is not None:
            return self.shape(shape, k=k)
        return self

    def tox(self, anchor_or_x) -> SchematicWireBuilder:
        """Append a horizontal segment ending at the target x coordinate."""
        self._require_unmaterialized()
        self._require_started()
        current_x, current_y = self._points[-1]
        self._append_point(
            (
                _schematic_axis_target(
                    self._axis_target_arg(anchor_or_x, "x"),
                    self._schematic._design,
                    "x",
                    schematic=self._schematic,
                    action="schematic wire",
                ),
                current_y,
            )
        )
        return self

    def toy(self, anchor_or_y) -> SchematicWireBuilder:
        """Append a vertical segment ending at the target y coordinate."""
        self._require_unmaterialized()
        self._require_started()
        current_x, current_y = self._points[-1]
        self._append_point(
            (
                current_x,
                _schematic_axis_target(
                    self._axis_target_arg(anchor_or_y, "y"),
                    self._schematic._design,
                    "y",
                    schematic=self._schematic,
                    action="schematic wire",
                ),
            )
        )
        return self

    def right(self, length: float) -> SchematicWireBuilder:
        return self._relative(dx=_coordinate(length), dy=0)

    def left(self, length: float) -> SchematicWireBuilder:
        return self._relative(dx=-_coordinate(length), dy=0)

    def up(self, length: float) -> SchematicWireBuilder:
        return self._relative(dx=0, dy=-_coordinate(length))

    def down(self, length: float) -> SchematicWireBuilder:
        return self._relative(dx=0, dy=_coordinate(length))

    def dot(self) -> SchematicWireBuilder:
        self._require_unmaterialized()
        self._dot_end = True
        return self

    def idot(self) -> SchematicWireBuilder:
        self._require_unmaterialized()
        self._dot_start = True
        return self

    def direct(self) -> SchematicWire:
        """Persist the collected points without inserting an automatic bend."""
        return self._persist(self._points, route_intent="Direct")

    def orthogonal(self) -> SchematicWire:
        """Persist the run, inserting one bend only for a two-point diagonal route."""
        return self._persist(
            _orthogonal_wire_points(self._points), route_intent="Orthogonal"
        )

    def shape(self, shape: str, *, k: float | None = None) -> SchematicWire:
        """Persist a SchemDraw-style point-to-point wire shape."""
        self._require_unmaterialized()
        self._require_started()
        if len(self._points) != 2:
            self._clear_pending()
            self._restore_drawing_state()
            raise ValueError(
                "Schematic wire shape routes need exactly two endpoints "
                f"for {_net_label(self._net)} on {_schematic_sheet_phrase(self._schematic)}"
            )
        try:
            shaped_points = _shape_wire_points(
                self._points[0],
                self._points[1],
                shape=shape,
                k=k,
            )
        except TypeError as error:
            self._clear_pending()
            self._restore_drawing_state()
            if str(error) == "Schematic wire shape must be a string":
                raise TypeError(
                    f"Invalid schematic wire shape for {_net_label(self._net)} on "
                    f"{_schematic_sheet_phrase(self._schematic)}: expected a string"
                ) from error
            raise
        except ValueError as error:
            self._clear_pending()
            self._restore_drawing_state()
            if str(error) == "Schematic wire shape must be one of -, -|, |-, |-|, n, -|-, or c":
                raise ValueError(
                    f"Invalid schematic wire shape {shape!r} for {_net_label(self._net)} on "
                    f"{_schematic_sheet_phrase(self._schematic)}; expected one of -, -|, "
                    "|-, |-|, n, -|-, or c"
                ) from error
            raise
        route_intent = "Direct" if shape == "-" else "Orthogonal"
        return self._persist(shaped_points, route_intent=route_intent, normalize=True)

    def _materialize(self) -> SchematicWire:
        try:
            if not _schematic_route_has_distinct_points(self._points):
                raise ValueError(
                    "Schematic drawing wire needs an endpoint before materialization"
                )
            return self._persist(self._points, route_intent="Direct", normalize=True)
        except Exception:
            self._clear_pending()
            self._restore_drawing_state()
            raise

    def _relative(self, *, dx: float, dy: float) -> SchematicWireBuilder:
        self._require_unmaterialized()
        self._require_started()
        current_x, current_y = self._points[-1]
        self._append_point((current_x + dx, current_y + dy))
        return self

    def _append_point(self, point: tuple[float, float]) -> None:
        self._points.append(point)
        self._update_drawing_cursor(point)

    def _point_for_authoring(self, point) -> tuple[float, float]:
        if self._drawing is not None:
            point = self._drawing._point_arg(point)
        return _schematic_point_for_authoring(
            point,
            design=self._schematic._design,
            schematic=self._schematic,
            action="schematic wire",
        )

    def _point_and_endpoint_for_authoring(
        self, point
    ) -> tuple[tuple[float, float], tuple[float, float, int | None, int | None]]:
        if self._drawing is not None:
            point = self._drawing._point_arg(point)
        endpoint = _schematic_endpoint_for_authoring(
            point,
            design=self._schematic._design,
            schematic=self._schematic,
            action="schematic wire",
        )
        return (endpoint[0], endpoint[1]), endpoint

    def _axis_target_arg(self, target, axis: str):
        if (
            self._drawing is not None
            and isinstance(target, (int, float))
            and not isinstance(target, bool)
        ):
            offset = self._drawing._coordinate_origin[0 if axis == "x" else 1]
            return _coordinate(target) + offset
        return target

    def _persist(
        self,
        points: Iterable[tuple[float, float]],
        *,
        route_intent: str,
        normalize: bool = False,
    ) -> SchematicWire:
        if self._wire is None:
            wire_points = (
                _normalize_schematic_route_points(points) if normalize else tuple(points)
            )
            try:
                self._wire = self._schematic._add_wire(
                    self._net,
                    wire_points,
                    route_intent=route_intent,
                    _authored_region=self._authored_region,
                    _endpoint_payloads=self._endpoint_payloads,
                )
                self._persist_endpoint_junctions(wire_points)
            except Exception:
                self._clear_pending()
                self._restore_drawing_state()
                raise
        self._clear_pending()
        return self._wire

    def _persist_endpoint_junctions(self, points: tuple[tuple[float, float], ...]) -> None:
        if self._dot_start:
            _add_schematic_junction_dot(
                self._schematic,
                points[0],
                net=self._net,
                _authored_region=self._authored_region,
                action="wire endpoint dot",
            )
        if self._dot_end:
            _add_schematic_junction_dot(
                self._schematic,
                points[-1],
                net=self._net,
                _authored_region=self._authored_region,
                action="wire endpoint dot",
            )

    def _update_drawing_cursor(self, point: tuple[float, float]) -> None:
        if self._drawing is not None:
            self._drawing._here = SchematicAnchor(
                point,
                design=self._schematic._design,
            )

    def _require_unmaterialized(self) -> None:
        if self._wire is not None:
            raise ValueError("Cannot modify a materialized schematic wire")

    def _require_started(self) -> None:
        if not self._points:
            raise ValueError("Schematic wire builder must start with from_()")

    def _clear_pending(self) -> None:
        if self._drawing is not None and self._drawing._pending is self:
            self._drawing._pending = None

    def _restore_drawing_state(self) -> None:
        if self._drawing is not None and self._start_here is not None:
            self._drawing._here = self._start_here
            self._drawing._direction = self._start_direction


class SchematicDrawing:
    """Cursor state for SchemDraw-style schematic authoring on one sheet."""

    def __init__(
        self,
        schematic: Schematic,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort = (0, 0),
        direction: str = "Right",
        unit: float = 20,
        coordinate_origin: tuple[float, float] = (0, 0),
        authored_region: int | None = None,
    ):
        self._schematic = schematic
        self._coordinate_origin = _schematic_point_tuple(coordinate_origin)
        self._authored_region = authored_region
        self._here = self._anchor_at(at)
        self._direction = _orientation(direction)
        self._unit = _coordinate(unit)
        if self._unit <= 0:
            raise ValueError("Schematic drawing unit must be positive")
        self._stack: list[tuple[SchematicAnchor, str]] = []
        self._pending: SchematicTwoTerminalElement | SchematicWireBuilder | None = None

    @property
    def here(self) -> SchematicAnchor:
        return self._here

    @property
    def direction(self) -> str:
        return self._direction

    @property
    def unit(self) -> float:
        return self._unit

    def move(self, *, dx: float = 0, dy: float = 0) -> SchematicDrawing:
        self._flush_pending()
        self._here = self._here.offset(dx=dx, dy=dy)
        return self

    def move_from(
        self,
        anchor: tuple[float, float] | SchematicAnchor | SchematicPort,
        *,
        dx: float = 0,
        dy: float = 0,
        direction: str | None = None,
    ) -> SchematicDrawing:
        self._flush_pending()
        next_direction = self._direction if direction is None else _orientation(direction)
        self._here = self._anchor_at(anchor).offset(dx=dx, dy=dy)
        self._direction = next_direction
        return self

    def push(self) -> SchematicDrawing:
        self._flush_pending()
        self._stack.append((self._here, self._direction))
        return self

    def pop(self) -> SchematicDrawing:
        self._flush_pending()
        if not self._stack:
            raise ValueError(
                "Cannot pop schematic drawing cursor state on "
                f"{_schematic_sheet_phrase(self._schematic)}: stack is empty"
            )
        self._here, self._direction = self._stack.pop()
        return self

    def two_terminal(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        reference_label: str | None = None,
    ) -> SchematicTwoTerminalElement:
        self._flush_pending()
        element = SchematicTwoTerminalElement(
            self,
            component,
            symbol=symbol,
            variant=variant,
            reference_label=reference_label,
        )
        self._pending = element
        return element

    def place(
        self,
        component: Component,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        orient: str | None = None,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        reference_label: str | None = None,
    ) -> PlacedSchematicElement:
        self._flush_pending()
        placed = self._schematic.place(
            component,
            at=self._here if at is None else self._point_arg(at),
            orient=self._direction if orient is None else orient,
            symbol=symbol,
            variant=variant,
            reference_label=reference_label,
            _authored_region=self._authored_region,
        )
        return PlacedSchematicElement(placed)

    def node(
        self,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        *,
        dx: float = 0,
        dy: float = 0,
    ) -> SchematicAnchor:
        """Return a reusable sheet-local geometry anchor without adding schematic objects."""
        base = self._here if at is None else self._anchor_at(at)
        return base.offset(dx=dx, dy=dy)

    def connect(
        self,
        *args,
        net: Net | None = None,
        shape: str | None = None,
        k: float | None = None,
    ) -> SchematicWire:
        """Project a wire between anchors, or over an explicit net through multiple anchors.

        ``connect(start, end, net=...)`` keeps the existing two-anchor behavior and can infer
        the net from connected pin anchors. ``connect(net, *anchors)`` requires an existing
        logical net first, validates every pin or port anchor against it, and projects one
        schematic wire run through the supplied geometry.
        """
        self._flush_pending()
        if args and isinstance(args[0], Net):
            if net is not None:
                raise ValueError("Pass schematic connection net either first or as net=, not both")
            return self._schematic.connect(
                args[0],
                *(self._point_arg(point) for point in args[1:]),
                shape=shape,
                k=k,
                _authored_region=self._authored_region,
            )
        if len(args) != 2:
            raise TypeError(
                "Schematic drawing connect expects start/end anchors, or a net followed by anchors"
            )
        start, end = args
        return self._schematic.connect(
            self._point_arg(start),
            self._point_arg(end),
            net=net,
            shape=shape,
            k=k,
            _authored_region=self._authored_region,
        )

    def ortho_lines(
        self,
        entries,
        *,
        shape: str | None = None,
        k: float | None = None,
    ) -> tuple[SchematicWire, ...]:
        self._flush_pending()
        localized = []
        for entry in entries:
            net, start, end = _schematic_ortho_line_entry_parts(entry)
            localized.append((net, self._point_arg(start), self._point_arg(end)))
        return self._schematic.ortho_lines(
            localized,
            shape=shape,
            k=k,
            _authored_region=self._authored_region,
        )

    def wire(self, net: Net) -> SchematicWireBuilder:
        self._flush_pending()
        builder = self._schematic.wire(net, _authored_region=self._authored_region)
        builder._drawing = self
        builder._start_here = self._here
        builder._start_direction = self._direction
        builder._authored_region = self._authored_region
        builder.from_(self._here)
        self._pending = builder
        return builder

    def line(self, net: Net) -> SchematicWireBuilder:
        return self.wire(net)

    def terminal(
        self,
        name_or_net: str | Net | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        net: Net | None = None,
        kind: str = "Power",
        orient: str | None = None,
    ) -> SchematicPort:
        """Place a generic one-terminal marker (power/ground symbol) on an existing net.

        Terminal markers are schematic presentation entities that visualize power, ground,
        or other single-connection net endpoints. They do not create or modify logical nets.

        Args:
            name_or_net: Display label, Net handle, or None. If a Net is provided, its name
                is used as the label. If None, the label is inferred from the net parameter
                or anchor net.
            at: Placement anchor. Pin or port anchors can infer the net if not explicitly
                provided. Coordinate tuples require an explicit net parameter.
            net: Explicit net binding. Cannot be combined with Net-typed name_or_net.
            kind: Marker visual style, either "Power" or "Ground" (case-insensitive).
            orient: Marker orientation ("Up", "Down", "Left", "Right"). Defaults to "Up"
                for Power markers and "Down" for Ground markers if not specified.

        Returns:
            SchematicPort handle to the placed terminal marker.

        Raises:
            ValueError: If coordinate anchor provided without explicit net, or if both
                name_or_net and net are Net handles, or if kind is invalid.
            TypeError: If name_or_net is not a string, Net, or None.
        """
        self._flush_pending()
        return self._schematic.terminal(
            name_or_net,
            at=self._here if at is None else self._point_arg(at),
            net=net,
            kind=kind,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def terminal_stub(
        self,
        name_or_net: str | Net | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        net: Net | None = None,
        kind: str = "Power",
        side: str | None = None,
        length: float = 8,
        orient: str | None = None,
    ) -> SchematicTerminalStub:
        """Draw a short wire from an anchor to a terminal marker on an existing net."""
        self._flush_pending()
        return self._schematic.terminal_stub(
            name_or_net,
            at=self._here if at is None else self._point_arg(at),
            net=net,
            kind=kind,
            side=side,
            length=length,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def power(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        net: Net | None = None,
        orient: str = "Up",
    ) -> SchematicPort:
        """Place a power marker symbol on an existing net.

        Convenience wrapper for terminal(name, kind="Power", ...).

        Args:
            name: Display label for the power marker.
            at: Placement anchor. Pin or port anchors can infer the net if not provided.
            net: Explicit net binding. Required for coordinate anchors.
            orient: Marker orientation. Defaults to "Up".

        Returns:
            SchematicPort handle to the placed power marker.
        """
        self._flush_pending()
        return self._schematic.power(
            name,
            at=self._here if at is None else self._point_arg(at),
            net=net,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def power_stub(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        net: Net | None = None,
        side: str = "Up",
        length: float = 8,
        orient: str = "Up",
    ) -> SchematicTerminalStub:
        """Draw a short wire from an anchor to a power marker on an existing net."""
        self._flush_pending()
        return self._schematic.power_stub(
            name,
            at=self._here if at is None else self._point_arg(at),
            net=net,
            side=side,
            length=length,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def ground(
        self,
        name: str | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        net: Net | None = None,
        orient: str = "Down",
    ) -> SchematicPort:
        """Place a ground marker symbol on an existing net.

        Convenience wrapper for terminal(name, kind="Ground", ...).

        Args:
            name: Display label for the ground marker. If None, uses the net name.
            at: Placement anchor. Pin or port anchors can infer the net if not provided.
            net: Explicit net binding. Required for coordinate anchors.
            orient: Marker orientation. Defaults to "Down".

        Returns:
            SchematicPort handle to the placed ground marker.
        """
        self._flush_pending()
        return self._schematic.ground(
            name,
            at=self._here if at is None else self._point_arg(at),
            net=net,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def ground_stub(
        self,
        name: str | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        net: Net | None = None,
        side: str = "Down",
        length: float = 8,
        orient: str = "Down",
    ) -> SchematicTerminalStub:
        """Draw a short wire from an anchor to a ground marker on an existing net."""
        self._flush_pending()
        return self._schematic.ground_stub(
            name,
            at=self._here if at is None else self._point_arg(at),
            net=net,
            side=side,
            length=length,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def net_label(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        orient: str = "Right",
        label: str | None = None,
    ) -> SchematicNetLabel:
        self._flush_pending()
        return self._schematic.net_label(
            name_or_net,
            at=self._here if at is None else self._point_arg(at),
            orient=orient,
            label=label,
            _authored_region=self._authored_region,
        )

    def local_label(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        side: str = "Right",
        offset: float = 2,
        orient: str | None = None,
    ) -> SchematicNetLabel:
        self._flush_pending()
        return self._schematic.local_label(
            name_or_net,
            at=self._here if at is None else self._point_arg(at),
            side=side,
            offset=offset,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def signal_stub(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        side: str | None = None,
        length: float = 8,
        label_gap: float = 2,
        orient: str | None = None,
        label: str | None = None,
    ) -> SchematicSignalStub:
        self._flush_pending()
        return self._schematic.signal_stub(
            name_or_net,
            at=self._here if at is None else self._point_arg(at),
            side=side,
            length=length,
            label_gap=label_gap,
            orient=orient,
            label=label,
            _authored_region=self._authored_region,
        )

    def signal_tag(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        side: str | None = None,
        length: float = 8,
        label: str | None = None,
        kind: str = "Bidirectional",
        orient: str | None = None,
    ) -> SchematicSignalTag:
        self._flush_pending()
        return self._schematic.signal_tag(
            name_or_net,
            at=self._here if at is None else self._point_arg(at),
            side=side,
            length=length,
            label=label,
            kind=kind,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def signal_tags(
        self,
        items,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        side: str | None = None,
        pitch: float = 8,
        length: float = 8,
        kind: str = "Bidirectional",
        orient: str | None = None,
    ) -> tuple[SchematicSignalTag, ...]:
        self._flush_pending()
        entries = self._signal_stub_items_arg(tuple(items))
        base_at = self._point_arg(at) if at is not None else None
        if base_at is None and any(not _signal_stub_entry_has_anchor(item) for item in entries):
            base_at = self._here
        return self._schematic.signal_tags(
            entries,
            at=base_at,
            side=side,
            pitch=pitch,
            length=length,
            kind=kind,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def signal_stubs(
        self,
        items,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        side: str | None = None,
        pitch: float = 8,
        length: float = 8,
        label_gap: float = 2,
        orient: str | None = None,
    ) -> tuple[SchematicSignalStub, ...]:
        self._flush_pending()
        entries = self._signal_stub_items_arg(tuple(items))
        base_at = self._point_arg(at) if at is not None else None
        if base_at is None and any(not _signal_stub_entry_has_anchor(item) for item in entries):
            base_at = self._here
        return self._schematic.signal_stubs(
            entries,
            at=base_at,
            side=side,
            pitch=pitch,
            length=length,
            label_gap=label_gap,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def stack(
        self,
        *,
        count: int,
        direction: str | None = None,
        pitch: float | None = None,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
    ) -> tuple[SchematicAnchor, ...]:
        if isinstance(count, bool) or not isinstance(count, int):
            raise TypeError("Schematic stack count must be an integer")
        if count < 0:
            raise ValueError("Schematic stack count must not be negative")
        stack_direction = self._direction if direction is None else _orientation(direction)
        stack_pitch = self._unit if pitch is None else _positive_coordinate(
            pitch, "Schematic stack pitches"
        )
        base = self._here if at is None else self._anchor_at(at)
        anchors = []
        for index in range(count):
            dx, dy = _schematic_direction_offset(stack_direction, stack_pitch * index)
            anchors.append(base.offset(dx=dx, dy=dy))
        return tuple(anchors)

    def junction(
        self,
        net: Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
    ) -> SchematicJunction:
        self._flush_pending()
        return self._schematic.junction(
            net,
            at=self._here if at is None else self._point_arg(at),
            _authored_region=self._authored_region,
        )

    def no_connect(
        self,
        anchor: SchematicPinAnchor,
        *,
        orient: str = "Right",
        offset: float = 0,
        reason: str | None = None,
    ) -> SchematicNoConnect:
        self._flush_pending()
        return self._schematic.no_connect(
            anchor,
            orient=orient,
            offset=offset,
            reason=reason,
            _authored_region=self._authored_region,
        )

    def sheet_port(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        net: Net | None = None,
        kind: str = "Bidirectional",
        orient: str = "Right",
    ) -> SchematicPort:
        self._flush_pending()
        return self._schematic.sheet_port(
            name,
            at=self._here if at is None else self._point_arg(at),
            net=net,
            kind=kind,
            orient=orient,
            _authored_region=self._authored_region,
        )

    def off_page(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        net: Net | None = None,
        orient: str = "Right",
    ) -> SchematicPort:
        self._flush_pending()
        return self._schematic.off_page(
            name,
            at=self._here if at is None else self._point_arg(at),
            net=net,
            orient=orient,
            _authored_region=self._authored_region,
        )

    @contextmanager
    def hold(self):
        self._flush_pending()
        saved_stack = list(self._stack)
        saved_here = self._here
        saved_direction = self._direction
        saved_pending = self._pending
        self.push()
        try:
            yield self
            self._flush_pending()
        finally:
            self._stack = saved_stack
            self._here = saved_here
            self._direction = saved_direction
            self._pending = saved_pending

    @contextmanager
    def frame(
        self,
        at: tuple[float, float] | SchematicAnchor | SchematicPort = (0, 0),
        *,
        direction: str | None = None,
    ):
        self._flush_pending()
        saved_origin = self._coordinate_origin
        saved_stack = list(self._stack)
        saved_here = self._here
        saved_direction = self._direction
        saved_pending = self._pending
        origin = self._anchor_at(at).point
        self._coordinate_origin = origin
        self._here = SchematicAnchor(origin, design=self._schematic._design)
        if direction is not None:
            self._direction = _orientation(direction)
        self._stack = []
        self._pending = None
        try:
            yield self
            self._flush_pending()
        finally:
            self._coordinate_origin = saved_origin
            self._stack = saved_stack
            self._here = saved_here
            self._direction = saved_direction
            self._pending = saved_pending

    def __enter__(self) -> SchematicDrawing:
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        if exc_type is None:
            self._flush_pending()
        return False

    def _anchor_at(
        self, value: tuple[float, float] | SchematicAnchor | SchematicPort
    ) -> SchematicAnchor:
        point = _schematic_point_for_authoring(
            value,
            design=self._schematic._design,
            schematic=self._schematic,
            action="drawing cursor",
        )
        if isinstance(value, (tuple, list)):
            point = (
                point[0] + self._coordinate_origin[0],
                point[1] + self._coordinate_origin[1],
            )
        return SchematicAnchor(point, design=self._schematic._design)

    def _point_arg(self, value):
        if isinstance(value, (tuple, list)):
            point = _schematic_point_tuple(value)
            return (
                point[0] + self._coordinate_origin[0],
                point[1] + self._coordinate_origin[1],
            )
        return value

    def _signal_stub_items_arg(self, items):
        entries = []
        for item in items:
            name_or_net, anchor, label = _signal_stub_entry_parts(item)
            if anchor is None:
                entries.append((name_or_net, label) if label is not None else name_or_net)
            elif label is None:
                entries.append((name_or_net, self._point_arg(anchor)))
            else:
                entries.append((name_or_net, self._point_arg(anchor), label))
        return tuple(entries)

    def _flush_pending(self) -> None:
        pending = self._pending
        if pending is not None:
            pending._materialize()
            if self._pending is pending:
                self._pending = None

    def __repr__(self) -> str:
        return (
            f"SchematicDrawing(here={self._here.point!r}, "
            f"direction={self._direction!r}, unit={self._unit!r})"
        )


class SchematicTwoTerminalElement:
    """Deferred fluent placement for one two-terminal schematic component."""

    def __init__(
        self,
        drawing: SchematicDrawing,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        reference_label: str | None = None,
    ):
        if not isinstance(component, Component):
            raise TypeError("Two-terminal placement expects a Component handle")
        if component._design is not drawing._schematic._design:
            raise ValueError(
                f"Component {component.reference} belongs to a different design while "
                f"authoring two-terminal placement on "
                f"{_schematic_sheet_phrase(drawing._schematic)}"
            )
        pins = tuple(component._pin_refs())
        if len(pins) != 2:
            raise ValueError("Two-terminal placement requires exactly two component pins")
        self._drawing = drawing
        self._component = component
        self._symbol = symbol
        self._variant = variant
        self._reference_label = reference_label
        self._start_here = drawing.here
        self._start_direction = drawing.direction
        self._at = drawing.here
        self._anchor_ref: str | int = "start"
        self._drop_ref: str | int = "end"
        self._orientation = drawing.direction
        self._length = drawing.unit
        self._reverse = False
        self._flip = False
        self._cursor_committed = False
        self._placed: PlacedSchematicElement | None = None

    @property
    def index(self) -> int:
        return self._materialize().index

    @property
    def component(self) -> Component:
        return self._component

    @property
    def orientation(self) -> str:
        return self._materialize().orientation

    @property
    def start(self) -> SchematicPinAnchor:
        return self._materialize().start

    @property
    def end(self) -> SchematicPinAnchor:
        return self._materialize().end

    @property
    def center(self) -> SchematicAnchor:
        return self._materialize().center

    def at(
        self, point: tuple[float, float] | SchematicAnchor | SchematicPort
    ) -> SchematicTwoTerminalElement:
        self._require_unplaced("at")
        self._at = self._drawing._anchor_at(point)
        self._commit_cursor()
        return self

    def anchor(self, ref: str | int) -> SchematicTwoTerminalElement:
        self._require_unplaced("anchor")
        self._anchor_ref = ref
        self._commit_cursor()
        return self

    def drop(self, ref: str | int) -> SchematicTwoTerminalElement:
        self._require_unplaced("drop")
        self._drop_ref = ref
        self._commit_cursor()
        return self

    def between(
        self,
        start: tuple[float, float] | SchematicAnchor | SchematicPort,
        end: tuple[float, float] | SchematicAnchor | SchematicPort,
        *,
        anchor: str | int = "start",
        drop: str | int = "end",
    ) -> SchematicTwoTerminalElement:
        self._require_unplaced("between")
        start_anchor = self._drawing._anchor_at(start)
        end_anchor = self._drawing._anchor_at(end)
        return self._configure_between(
            start_anchor,
            end_anchor,
            method="between",
            anchor=anchor,
            drop=drop,
        )

    def endpoints(
        self,
        start: tuple[float, float] | SchematicAnchor | SchematicPort,
        end: tuple[float, float] | SchematicAnchor | SchematicPort,
        *,
        anchor: str | int = "start",
        drop: str | int = "end",
    ) -> SchematicTwoTerminalElement:
        return self.between(start, end, anchor=anchor, drop=drop)

    def to(
        self, end: tuple[float, float] | SchematicAnchor | SchematicPort
    ) -> SchematicTwoTerminalElement:
        self._require_unplaced("to")
        return self._configure_between(
            self._at,
            self._drawing._anchor_at(end),
            method="to",
        )

    def tox(self, anchor_or_x) -> SchematicTwoTerminalElement:
        self._require_unplaced("tox")
        target_x = _schematic_axis_target(
            self._axis_target_arg(anchor_or_x, "x"),
            self._drawing._schematic._design,
            "x",
            schematic=self._drawing._schematic,
            action="two-terminal placement",
        )
        return self.to(
            SchematicAnchor((target_x, self._at.y), design=self._component._design)
        )

    def toy(self, anchor_or_y) -> SchematicTwoTerminalElement:
        self._require_unplaced("toy")
        target_y = _schematic_axis_target(
            self._axis_target_arg(anchor_or_y, "y"),
            self._drawing._schematic._design,
            "y",
            schematic=self._drawing._schematic,
            action="two-terminal placement",
        )
        return self.to(
            SchematicAnchor((self._at.x, target_y), design=self._component._design)
        )

    def _configure_between(
        self,
        start_anchor: SchematicAnchor,
        end_anchor: SchematicAnchor,
        *,
        method: str,
        anchor: str | int | None = None,
        drop: str | int | None = None,
    ) -> SchematicTwoTerminalElement:
        dx = end_anchor.x - start_anchor.x
        dy = end_anchor.y - start_anchor.y
        if dx == 0 and dy == 0:
            raise ValueError(f"Two-terminal {method}() anchors must be distinct")
        if dx != 0 and dy != 0:
            raise ValueError(
                f"Two-terminal {method}() anchors must be horizontally or vertically aligned"
            )

        self._at = start_anchor
        if anchor is not None:
            self._anchor_ref = anchor
        if drop is not None:
            self._drop_ref = drop
        if dx != 0:
            self._orientation = "Right" if dx > 0 else "Left"
            self._length = abs(dx)
        else:
            self._orientation = "Down" if dy > 0 else "Up"
            self._length = abs(dy)
        self._commit_cursor()
        return self

    def length(self, value: float) -> SchematicTwoTerminalElement:
        self._require_unplaced("length")
        self._length = self._length_from_units(value)
        self._commit_cursor()
        return self

    def right(self, length: float | None = None) -> SchematicTwoTerminalElement:
        return self._direction("Right", length)

    def left(self, length: float | None = None) -> SchematicTwoTerminalElement:
        return self._direction("Left", length)

    def up(self, length: float | None = None) -> SchematicTwoTerminalElement:
        return self._direction("Up", length)

    def down(self, length: float | None = None) -> SchematicTwoTerminalElement:
        return self._direction("Down", length)

    def reverse(self) -> SchematicTwoTerminalElement:
        self._require_unplaced("reverse")
        self._reverse = not self._reverse
        self._commit_cursor()
        return self

    def flip(self) -> SchematicTwoTerminalElement:
        self._require_unplaced("flip")
        self._flip = not self._flip
        self._commit_cursor()
        return self

    def label(
        self,
        text: str,
        *,
        loc: str | None = None,
        name: str = "label",
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
    ) -> PlacedSchematicElement:
        return self._materialize().label(
            text, loc=loc, name=name, offset=offset, ofst=ofst, orient=orient
        )

    def label_ref(
        self,
        *,
        loc: str | None = None,
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
    ) -> PlacedSchematicElement:
        return self._materialize().label_ref(
            loc=loc, offset=offset, ofst=ofst, orient=orient
        )

    def label_value(
        self,
        *,
        loc: str | None = None,
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
    ) -> PlacedSchematicElement:
        return self._materialize().label_value(
            loc=loc, offset=offset, ofst=ofst, orient=orient
        )

    def dot(self, *, net: Net | None = None) -> PlacedSchematicElement:
        return self._materialize().dot(net=net)

    def idot(self, *, net: Net | None = None) -> PlacedSchematicElement:
        return self._materialize().idot(net=net)

    def __getitem__(self, key: int | str) -> SchematicPinAnchor:
        return self._materialize()[key]

    def __getattr__(self, name: str) -> SchematicPinAnchor:
        return getattr(self._materialize(), name)

    def pin_anchor(self, number: int | str) -> tuple[float, float]:
        return self._materialize().pin_anchor(number)

    def pin(self, key: int | str) -> SchematicPinAnchor:
        return self._materialize().pin(key)

    def pins(self, name: str) -> tuple[SchematicPinAnchor, ...]:
        return self._materialize().pins(name)

    def pin_anchors(self) -> tuple[SchematicPinAnchor, ...]:
        return self._materialize().pin_anchors()

    def _direction(
        self, orientation: str, length: float | None
    ) -> SchematicTwoTerminalElement:
        self._require_unplaced(orientation.lower())
        self._orientation = _orientation(orientation)
        if length is not None:
            self._length = self._length_from_units(length)
        self._commit_cursor()
        return self

    def _length_from_units(self, value: float) -> float:
        length = _coordinate(value) * self._drawing.unit
        if length <= 0:
            raise ValueError("Two-terminal element length must be positive")
        return length

    def _axis_target_arg(self, target, axis: str):
        if isinstance(target, (int, float)) and not isinstance(target, bool):
            offset = self._drawing._coordinate_origin[0 if axis == "x" else 1]
            return _coordinate(target) + offset
        return target

    def _require_unplaced(self, method: str) -> None:
        if self._placed is not None:
            raise ValueError(f"Cannot call {method}() after two-terminal placement is materialized")

    def _commit_cursor(self) -> None:
        try:
            origin = self._origin()
            drop = _transform_symbol_point(
                self._local_anchor(self._drop_ref), origin, self._orientation
            )
            self._drawing._here = SchematicAnchor(drop, design=self._component._design)
            self._drawing._direction = self._orientation
            self._cursor_committed = True
        except Exception:
            if self._drawing._pending is self:
                self._drawing._pending = None
            self._restore_drawing_state()
            raise

    def _materialize(self) -> PlacedSchematicElement:
        if self._placed is not None:
            return self._placed
        try:
            origin = self._origin()
            symbol = self._placement_symbol()
            placed = self._drawing._schematic.place(
                self._component,
                at=origin,
                orient=self._orientation,
                symbol=symbol,
                variant=self._variant,
                reference_label=self._reference_label,
                _authored_region=self._drawing._authored_region,
            )
            self._placed = PlacedSchematicElement(placed)
            if not self._cursor_committed:
                self._commit_cursor()
            if self._drawing._pending is self:
                self._drawing._pending = None
            return self._placed
        except Exception:
            if self._drawing._pending is self:
                self._drawing._pending = None
            self._restore_drawing_state()
            raise

    def _restore_drawing_state(self) -> None:
        if self._cursor_committed:
            self._drawing._here = self._start_here
            self._drawing._direction = self._start_direction
            self._cursor_committed = False

    def _origin(self) -> tuple[float, float]:
        aligned = self._at.point
        anchor = self._local_anchor(self._anchor_ref)
        rotated = _rotate_symbol_point(anchor, self._orientation)
        return (aligned[0] - rotated[0], aligned[1] - rotated[1])

    def _local_anchor(self, ref: str | int) -> tuple[float, float]:
        pins = self._presentation_pins()
        if isinstance(ref, str):
            normalized = ref.casefold()
            if normalized == "start":
                return pins[0]["at"]
            if normalized == "end":
                return pins[-1]["at"]
            if normalized == "center":
                start = pins[0]["at"]
                end = pins[-1]["at"]
                return ((start[0] + end[0]) / 2, (start[1] + end[1]) / 2)

        matches = _presentation_pin_matches(pins, ref)
        if len(matches) == 1:
            return matches[0]["at"]
        if len(matches) > 1:
            numbers = ", ".join(f"{item['number']!r}" for item in matches)
            raise ValueError(
                f"Two-terminal anchor {ref!r} is ambiguous for component "
                f"{self._component.reference} on "
                f"{_schematic_sheet_phrase(self._drawing._schematic)}; use a pin number. "
                f"Matching pin numbers: {numbers}"
            )
        raise ValueError(f"Two-terminal element has no anchor named {ref!r}")

    def _presentation_pins(self) -> tuple[dict, ...]:
        spec = self._base_symbol_spec()
        if spec is not None:
            return _presentation_symbol_pins(
                spec,
                length=self._length,
                reverse=self._reverse,
                flip=self._flip,
            )

        pins = tuple(self._component._pin_refs())
        local = (
            {
                "name": pins[0]["name"],
                "number": pins[0]["number"],
                "at": (0.0, 0.0),
                "orientation": "Left",
            },
            {
                "name": pins[1]["name"],
                "number": pins[1]["number"],
                "at": (20.0, 0.0),
                "orientation": "Right",
            },
        )
        if self._reverse or self._flip or self._length != 20.0:
            raise ValueError(
                "Two-terminal placement cannot adjust an unknown schematic symbol"
            )
        return local

    def _placement_symbol(self) -> str | SchematicSymbolSpec | None:
        symbol = self._base_symbol()
        spec = self._base_symbol_spec()
        if spec is None:
            return self._symbol
        if not _needs_generated_two_terminal_symbol(
            spec, length=self._length, reverse=self._reverse, flip=self._flip
        ):
            return symbol
        return _presentation_symbol_spec(
            spec,
            length=self._length,
            reverse=self._reverse,
            flip=self._flip,
        )

    def _base_symbol(self) -> str | SchematicSymbolSpec:
        symbol = self._symbol
        if symbol is None:
            symbol = self._component.schematic_symbol_variant(self._variant)
            if symbol is None:
                raise ValueError(
                    _missing_schematic_symbol_message(
                        self._component,
                        self._drawing._schematic,
                        self._variant,
                    )
                )
        if isinstance(symbol, (str, SchematicSymbolSpec)):
            return symbol
        raise TypeError("symbol must be a string or SchematicSymbolSpec")

    def _base_symbol_spec(self) -> SchematicSymbolSpec | None:
        symbol = self._base_symbol()
        if isinstance(symbol, SchematicSymbolSpec):
            return symbol
        return _default_two_terminal_symbol_spec(symbol)

    def __dir__(self) -> list[str]:
        result = set(super().__dir__())
        result.update(
            {
                "anchor",
                "at",
                "center",
                "component",
                "down",
                "drop",
                "end",
                "endpoints",
                "flip",
                "dot",
                "idot",
                "index",
                "label",
                "label_ref",
                "label_value",
                "left",
                "length",
                "between",
                "orientation",
                "pin",
                "pin_anchor",
                "pin_anchors",
                "pins",
                "reverse",
                "right",
                "start",
                "to",
                "tox",
                "toy",
                "up",
            }
        )
        try:
            pin_names = [pin["name"] for pin in self._presentation_pins()]
        except Exception:
            pin_names = []
        result.update(
            name for name in pin_names if name.isidentifier() and pin_names.count(name) == 1
        )
        return sorted(result)

    def __repr__(self) -> str:
        return (
            f"SchematicTwoTerminalElement(component={self._component!r}, "
            f"orientation={self._orientation!r})"
        )


class Schematic:
    """Handle to kernel-owned schematic projection data for one sheet."""

    def __init__(self, design: Design, sheet_index: int, name: str):
        self._design = design
        self._sheet_index = sheet_index
        self.name = name

    @property
    def sheet_index(self) -> int:
        return self._sheet_index

    def drawing(
        self,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort = (0, 0),
        direction: str = "Right",
        unit: float = 20,
    ) -> SchematicDrawing:
        return SchematicDrawing(self, at=at, direction=direction, unit=unit)

    def region(
        self,
        name: str,
        *,
        x: float,
        y: float,
        w: float,
        h: float,
        title: str | None = None,
        style: dict[str, str] | None = None,
    ) -> SchematicRegion:
        if not isinstance(name, str):
            raise TypeError("Schematic region names must be strings")
        if not name:
            raise ValueError("Schematic region names must not be empty")
        region_title = name if title is None else title
        if not isinstance(region_title, str):
            raise TypeError("Schematic region titles must be strings")
        if not region_title:
            raise ValueError("Schematic region titles must not be empty")
        bounds = {
            "x": _coordinate(x),
            "y": _coordinate(y),
            "width": _positive_coordinate(w, "Schematic region widths"),
            "height": _positive_coordinate(h, "Schematic region heights"),
        }
        region_style = _string_dict(style or {}, "Schematic region style")
        index = self._design._circuit.schematic_region(
            self._sheet_index,
            {
                "name": name,
                "title": region_title,
                "bounds": bounds,
                "style": region_style,
            },
        )
        return SchematicRegion(
            self,
            index,
            name=name,
            title=region_title,
            bounds=(bounds["x"], bounds["y"], bounds["width"], bounds["height"]),
            style=region_style,
        )

    def place(
        self,
        component: Component,
        *,
        at: tuple[float, float],
        orient: str = "Right",
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        _authored_region: int | None = None,
        reference_label: str | None = None,
    ) -> SchematicSymbol:
        if not isinstance(component, Component):
            raise TypeError("Schematic placement expects a Component handle")
        if component._design is not self._design:
            raise ValueError(
                f"Component {component.reference} belongs to a different design while "
                f"authoring symbol placement on {_schematic_sheet_phrase(self)}"
            )
        if symbol is None:
            symbol = component.schematic_symbol_variant(variant)
            if symbol is None:
                raise ValueError(_missing_schematic_symbol_message(component, self, variant))
        if isinstance(symbol, SchematicSymbolSpec):
            self.register_symbol(symbol)
            symbol_name = symbol.name
        elif isinstance(symbol, str):
            symbol_name = symbol
        else:
            raise TypeError("symbol must be a string or SchematicSymbolSpec")
        if not symbol_name:
            raise ValueError("symbol must not be empty")
        if reference_label is not None:
            if not isinstance(reference_label, str):
                raise TypeError("Schematic symbol reference labels must be strings")
            if not reference_label:
                raise ValueError("Schematic symbol reference labels must not be empty")
        orientation = _orientation(orient)
        x, y = _schematic_point_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="symbol placement",
        )
        instance = self._design._circuit.place_schematic_symbol(
            self._sheet_index,
            component.index,
            symbol_name,
            x,
            y,
            orientation,
            _authored_region,
        )
        placed = SchematicSymbol(self, instance, component, orientation, _authored_region)
        if reference_label is not None:
            PlacedSchematicElement(placed).label(reference_label, name="reference")
        return placed

    def register_symbol(self, symbol: SchematicSymbolSpec) -> None:
        if not isinstance(symbol, SchematicSymbolSpec):
            raise TypeError("register_symbol expects a SchematicSymbolSpec")
        self._design._register_schematic_symbol(symbol)

    def wire(
        self,
        net: Net,
        points: Iterable[tuple[float, float] | SchematicAnchor | SchematicPort] | None = None,
        *,
        _authored_region: int | None = None,
    ) -> SchematicWire | SchematicWireBuilder:
        if not isinstance(net, Net):
            raise TypeError("Schematic wires expect a Net handle")
        if net._design is not self._design:
            raise ValueError(
                _with_schematic_context(
                    _cross_design_net_message(net), self, "schematic wire"
                )
            )
        if points is None:
            return SchematicWireBuilder(self, net, authored_region=_authored_region)

        return self._add_wire(
            net, points, route_intent="Direct", _authored_region=_authored_region
        )

    def connect(
        self,
        *args,
        net: Net | None = None,
        shape: str | None = None,
        k: float | None = None,
        _authored_region: int | None = None,
    ) -> SchematicWire:
        """Project a wire between anchors, or over an explicit net through multiple anchors.

        ``connect(start, end, net=...)`` keeps the existing two-anchor behavior and can infer
        the net from connected pin anchors. ``connect(net, *anchors)`` requires an existing
        logical net first, validates every pin or port anchor against it, and projects one
        schematic wire run through the supplied geometry.
        """
        if args and isinstance(args[0], Net):
            if net is not None:
                raise ValueError("Pass schematic connection net either first or as net=, not both")
            return self._connect_existing_net(
                args[0],
                args[1:],
                shape=shape,
                k=k,
                _authored_region=_authored_region,
            )
        if len(args) != 2:
            raise TypeError(
                "Schematic connect expects start/end anchors, or a net followed by anchors"
            )
        start, end = args
        try:
            explicit = _validate_explicit_schematic_net(
                self._design, net, type_message="Schematic connections expect a Net handle"
            )
        except ValueError as error:
            _raise_cross_design_with_context(error, self, "schematic wire")
        endpoints = tuple(
            _schematic_endpoint_for_authoring(
                point,
                design=self._design,
                schematic=self,
                action="schematic wire",
            )
            for point in (start, end)
        )
        points = tuple((endpoint[0], endpoint[1]) for endpoint in endpoints)
        if shape is None:
            route_points = _orthogonal_wire_points(points)
            route_intent = "Orthogonal"
        else:
            route_points = _shape_wire_points(points[0], points[1], shape=shape, k=k)
            route_intent = "Direct" if shape == "-" else "Orthogonal"
        return self._add_wire_with_endpoint_payloads(
            explicit,
            route_points,
            endpoints,
            route_intent=route_intent,
            _authored_region=_authored_region,
        )

    def _connect_existing_net(
        self,
        net: Net,
        anchors,
        *,
        shape: str | None,
        k: float | None,
        _authored_region: int | None = None,
    ) -> SchematicWire:
        if not isinstance(net, Net):
            raise TypeError("Schematic connections expect a Net handle")
        if net._design is not self._design:
            raise ValueError(
                _with_schematic_context(_cross_design_net_message(net), self, "schematic wire")
            )
        points = tuple(anchors)
        if len(points) < 2:
            raise ValueError("Schematic net connections need at least two anchors")
        endpoints = tuple(
            _schematic_endpoint_for_authoring(
                point,
                design=self._design,
                schematic=self,
                action="schematic wire",
            )
            for point in points
        )
        converted = tuple((endpoint[0], endpoint[1]) for endpoint in endpoints)
        if shape is None:
            return self._add_wire_with_endpoint_payloads(
                net,
                _multi_anchor_orthogonal_wire_points(converted),
                endpoints,
                route_intent="Orthogonal",
                _authored_region=_authored_region,
            )
        return self._add_wire_with_endpoint_payloads(
            net,
            _multi_anchor_shape_wire_points(converted, shape=shape, k=k),
            endpoints,
            route_intent="Direct" if shape == "-" else "Orthogonal",
            _authored_region=_authored_region,
        )

    def ortho_lines(
        self,
        entries,
        *,
        shape: str | None = None,
        k: float | None = None,
        _authored_region: int | None = None,
    ) -> tuple[SchematicWire, ...]:
        wires = []
        for entry in entries:
            net, start, end = _schematic_ortho_line_entry_parts(entry)
            wires.append(
                self.connect(
                    start,
                    end,
                    net=net,
                    shape=shape,
                    k=k,
                    _authored_region=_authored_region,
                )
            )
        return tuple(wires)

    def _add_wire_with_endpoint_payloads(
        self,
        net: Net | None,
        points: Iterable[tuple[float, float]],
        endpoint_payloads: Iterable[tuple[float, float, int | None, int | None]],
        *,
        route_intent: str,
        _authored_region: int | None = None,
        _points_already_converted: bool = False,
        action: str = "schematic wire",
    ) -> SchematicWire:
        wire_points = tuple(points)
        if len(wire_points) < 2:
            raise ValueError("Schematic wires need at least two points")
        if net is not None:
            _require_schematic_net(
                net,
                self._design,
                type_message="Schematic wires expect a Net handle",
            )
        if _points_already_converted:
            converted = wire_points
        else:
            converted = tuple(
                _schematic_point_for_authoring(
                    point,
                    design=self._design,
                    schematic=self,
                    action=action,
                )
                for point in wire_points
            )

        try:
            wire, resolved_net_index = self._design._circuit.add_schematic_wire_for_endpoints(
                self._sheet_index,
                None if net is None else net.index,
                converted,
                list(endpoint_payloads),
                route_intent,
                _authored_region,
            )
        except ValueError as error:
            raise ValueError(
                _with_schematic_context(str(error), self, action)
            ) from error
        resolved_net = (
            net if net is not None else _net_by_index(self._design, resolved_net_index)
        )
        return SchematicWire(
            self,
            wire,
            net=resolved_net,
            points=tuple(converted),
            authored_region=_authored_region,
        )

    def _add_wire(
        self,
        net: Net,
        points: Iterable[tuple[float, float]],
        *,
        route_intent: str,
        _authored_region: int | None = None,
        _endpoint_payloads: Iterable[tuple[float, float, int | None, int | None]] | None = None,
    ) -> SchematicWire:
        wire_points = tuple(points)
        if len(wire_points) < 2:
            raise ValueError("Schematic wires need at least two points")

        if _endpoint_payloads is None:
            endpoints = tuple(
                _schematic_endpoint_for_authoring(
                    point,
                    design=self._design,
                    schematic=self,
                    action="schematic wire",
                )
                for point in wire_points
            )
            converted = tuple((endpoint[0], endpoint[1]) for endpoint in endpoints)
        else:
            endpoints = tuple(_endpoint_payloads)
            converted = wire_points

        return self._add_wire_with_endpoint_payloads(
            net,
            converted,
            endpoints,
            route_intent=route_intent,
            _authored_region=_authored_region,
            _points_already_converted=True,
        )

    def label(
        self,
        net: Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
        label: str | None = None,
        _authored_region: int | None = None,
        align: str = "start",
        baseline: str = "baseline",
        font_size: float | None = None,
    ) -> SchematicNetLabel:
        if not isinstance(net, Net):
            raise TypeError("Schematic labels expect a Net handle")
        if net._design is not self._design:
            raise ValueError(
                _with_schematic_context(_cross_design_net_message(net), self, "net label")
            )
        endpoint = _schematic_endpoint_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="net label",
        )
        return self._add_net_label_with_endpoint(
            net,
            endpoint,
            orient=orient,
            label=label,
            _authored_region=_authored_region,
            align=align,
            baseline=baseline,
            font_size=font_size,
            action="net label",
        )

    def _add_net_label_with_endpoint(
        self,
        net: Net,
        endpoint: tuple[float, float, int | None, int | None],
        *,
        orient: str,
        label: str | None = None,
        _authored_region: int | None = None,
        align: str = "start",
        baseline: str = "baseline",
        font_size: float | None = None,
        action: str,
    ) -> SchematicNetLabel:
        orientation = _orientation(orient)

        try:
            label, _resolved_net_index = self._design._circuit.add_schematic_net_label_for_endpoint(
                self._sheet_index,
                net.index,
                endpoint,
                orientation,
                _authored_region,
                _optional_display_label(label),
                _text_horizontal_alignment(align),
                _text_vertical_alignment(baseline),
                _optional_text_font_size(font_size),
            )
        except ValueError as error:
            raise ValueError(_with_schematic_context(str(error), self, action)) from error
        return SchematicNetLabel(self, label, orientation)

    def net_label(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
        label: str | None = None,
        _authored_region: int | None = None,
        align: str = "start",
        baseline: str = "baseline",
        font_size: float | None = None,
    ) -> SchematicNetLabel:
        try:
            net = _resolve_schematic_net_label(self._design, name_or_net)
        except ValueError as error:
            _raise_cross_design_with_context(error, self, "net label")
        return self.label(
            net,
            at=at,
            orient=orient,
            label=label,
            _authored_region=_authored_region,
            align=align,
            baseline=baseline,
            font_size=font_size,
        )

    def local_label(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        side: str = "Right",
        offset: float = 2,
        orient: str | None = None,
        _authored_region: int | None = None,
    ) -> SchematicNetLabel:
        side_orientation = _orientation(side)
        label_orientation = side_orientation if orient is None else _orientation(orient)
        try:
            net = _resolve_schematic_net_label(self._design, name_or_net)
        except ValueError as error:
            _raise_cross_design_with_context(error, self, "local net label")
        endpoint = _schematic_endpoint_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="local net label",
        )
        anchor = (endpoint[0], endpoint[1])
        position = _offset_schematic_point(
            anchor,
            side_orientation,
            _nonnegative_coordinate(offset, "Local net label offsets"),
        )
        return self._add_net_label_with_endpoint(
            net,
            _retarget_schematic_endpoint(endpoint, position),
            orient=label_orientation,
            _authored_region=_authored_region,
            action="local net label",
        )

    def signal_stub(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        side: str | None = None,
        length: float = 8,
        label_gap: float = 2,
        orient: str | None = None,
        label: str | None = None,
        _authored_region: int | None = None,
    ) -> SchematicSignalStub:
        side_orientation = _signal_stub_side(side, at)
        label_orientation = side_orientation if orient is None else _orientation(orient)
        try:
            net = _resolve_schematic_net_label(self._design, name_or_net)
        except ValueError as error:
            _raise_cross_design_with_context(error, self, "signal stub")
        endpoint = _schematic_endpoint_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="signal stub",
        )
        start = (endpoint[0], endpoint[1])
        end = _offset_schematic_point(
            start,
            side_orientation,
            _positive_coordinate(length, "Signal stub lengths"),
        )
        label_position = _offset_schematic_point(
            end,
            side_orientation,
            _nonnegative_coordinate(label_gap, "Signal stub label gaps"),
        )
        wire = self._add_wire_with_endpoint_payloads(
            net,
            (start, end),
            (endpoint, (end[0], end[1], None, None)),
            route_intent="Direct",
            _authored_region=_authored_region,
            action="signal stub",
        )
        label = self._add_net_label_with_endpoint(
            net,
            _retarget_schematic_endpoint(endpoint, label_position),
            orient=label_orientation,
            label=label,
            _authored_region=_authored_region,
            action="signal stub label",
        )
        return SchematicSignalStub(
            self,
            net=net,
            side=side_orientation,
            wire=wire,
            label=label,
            start=start,
            end=end,
            label_position=label_position,
        )

    def signal_tag(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        side: str | None = None,
        length: float = 8,
        label: str | None = None,
        kind: str = "Bidirectional",
        orient: str | None = None,
        _authored_region: int | None = None,
    ) -> SchematicSignalTag:
        side_orientation = _signal_stub_side(side, at)
        tag_orientation = side_orientation if orient is None else _orientation(orient)
        try:
            net = _resolve_schematic_net_label(self._design, name_or_net)
        except ValueError as error:
            _raise_cross_design_with_context(error, self, "signal tag")
        endpoint = _schematic_endpoint_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="signal tag",
        )
        start = (endpoint[0], endpoint[1])
        end = _offset_schematic_point(
            start,
            side_orientation,
            _positive_coordinate(length, "Signal tag lengths"),
        )
        wire = self._add_wire_with_endpoint_payloads(
            net,
            (start, end),
            (endpoint, (end[0], end[1], None, None)),
            route_intent="Direct",
            _authored_region=_authored_region,
            action="signal tag",
        )
        port = self.sheet_port(
            label or net.name,
            at=end,
            net=net,
            kind=kind,
            orient=tag_orientation,
            _authored_region=_authored_region,
        )
        return SchematicSignalTag(
            self,
            net=net,
            side=side_orientation,
            wire=wire,
            port=port,
            start=start,
            end=end,
        )

    def signal_tags(
        self,
        items,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        side: str | None = None,
        pitch: float = 8,
        length: float = 8,
        kind: str = "Bidirectional",
        orient: str | None = None,
        _authored_region: int | None = None,
    ) -> tuple[SchematicSignalTag, ...]:
        side_orientation = (
            _signal_stub_side(side, at)
            if at is not None
            else _orientation("Right" if side is None else side)
        )
        entries = tuple(items)
        if not entries:
            return ()
        starts = _signal_stub_entries(
            entries,
            at=at,
            side=side_orientation,
            pitch=_positive_coordinate(pitch, "Signal tag pitches"),
        )
        return tuple(
            self.signal_tag(
                name_or_net,
                at=anchor,
                side=side if side is not None or not generated else side_orientation,
                length=length,
                label=label,
                kind=kind,
                orient=orient,
                _authored_region=_authored_region,
            )
            for name_or_net, anchor, label, generated in starts
        )

    def signal_stubs(
        self,
        items,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        side: str | None = None,
        pitch: float = 8,
        length: float = 8,
        label_gap: float = 2,
        orient: str | None = None,
        _authored_region: int | None = None,
    ) -> tuple[SchematicSignalStub, ...]:
        side_orientation = (
            _signal_stub_side(side, at)
            if at is not None
            else _orientation("Right" if side is None else side)
        )
        entries = tuple(items)
        if not entries:
            return ()
        starts = _signal_stub_entries(
            entries,
            at=at,
            side=side_orientation,
            pitch=_positive_coordinate(pitch, "Signal stub pitches"),
        )
        return tuple(
            self.signal_stub(
                name_or_net,
                at=anchor,
                side=side if side is not None or not generated else side_orientation,
                length=length,
                label_gap=label_gap,
                orient=orient,
                label=label,
                _authored_region=_authored_region,
            )
            for name_or_net, anchor, label, generated in starts
        )

    def _add_symbol_field(
        self,
        symbol: SchematicSymbol,
        *,
        name: str,
        value: str,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
        _authored_region: int | None = None,
        align: str = "middle",
        baseline: str = "baseline",
        font_size: float | None = None,
    ) -> SchematicSymbolField:
        if not isinstance(symbol, SchematicSymbol):
            raise TypeError("Schematic symbol fields expect a placed symbol handle")
        if symbol._schematic is not self:
            raise ValueError("Schematic symbol belongs to a different schematic")
        x, y = _schematic_point_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="symbol field",
        )
        orientation = _orientation(orient)

        field = self._design._circuit.add_schematic_symbol_field(
            self._sheet_index,
            symbol.index,
            name,
            value,
            x,
            y,
            orientation,
            _authored_region,
            _text_horizontal_alignment(align),
            _text_vertical_alignment(baseline),
            _optional_text_font_size(font_size),
        )
        return SchematicSymbolField(
            self,
            field,
            symbol=symbol,
            name=name,
            value=value,
            at=(x, y),
            orientation=orientation,
        )

    def junction(
        self,
        net: Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        _authored_region: int | None = None,
    ) -> SchematicJunction:
        if not isinstance(net, Net):
            raise TypeError("Schematic junctions expect a Net handle")
        if net._design is not self._design:
            raise ValueError(
                _with_schematic_context(_cross_design_net_message(net), self, "junction")
            )
        endpoint = _schematic_endpoint_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="junction",
        )
        try:
            junction, _resolved_net_index = (
                self._design._circuit.add_schematic_junction_for_endpoint(
                    self._sheet_index, net.index, endpoint, _authored_region
                )
            )
        except ValueError as error:
            raise ValueError(_with_schematic_context(str(error), self, "junction")) from error
        return SchematicJunction(self, junction)

    def terminal(
        self,
        name_or_net: str | Net | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        kind: str = "Power",
        orient: str | None = None,
        _authored_region: int | None = None,
    ) -> SchematicPort:
        """Place a generic one-terminal marker (power/ground symbol) on an existing net.

        Terminal markers are schematic presentation entities that visualize power, ground,
        or other single-connection net endpoints. They do not create or modify logical nets.

        Args:
            name_or_net: Display label, Net handle, or None. If a Net is provided, its name
                is used as the label. If None, the label is inferred from the net parameter
                or anchor net.
            at: Placement anchor. Pin or port anchors can infer the net if not explicitly
                provided. Coordinate tuples require an explicit net parameter.
            net: Explicit net binding. Cannot be combined with Net-typed name_or_net.
            kind: Marker visual style, either "Power" or "Ground" (case-insensitive).
            orient: Marker orientation ("Up", "Down", "Left", "Right"). Defaults to "Up"
                for Power markers and "Down" for Ground markers if not specified.

        Returns:
            SchematicPort handle to the placed terminal marker.

        Raises:
            ValueError: If coordinate anchor provided without explicit net, or if both
                name_or_net and net are Net handles, or if kind is invalid.
            TypeError: If name_or_net is not a string, Net, or None.
        """
        marker_kind = _terminal_marker_kind(kind)
        if isinstance(name_or_net, Net):
            if net is not None:
                raise ValueError("Pass terminal marker net either first or as net=, not both")
            try:
                marker_net = _validate_explicit_schematic_net(
                    self._design,
                    name_or_net,
                    type_message="Schematic terminal markers expect a Net handle",
                )
            except ValueError as error:
                _raise_cross_design_with_context(error, self, "terminal marker")
            name = marker_net.name
        else:
            if name_or_net is not None and not isinstance(name_or_net, str):
                raise TypeError("Schematic terminal markers expect a name string, Net handle, or None")
            if isinstance(name_or_net, str) and not name_or_net:
                raise ValueError("Schematic terminal marker names must not be empty")
            try:
                marker_net = _validate_explicit_schematic_net(
                    self._design,
                    net,
                    type_message="Schematic terminal markers expect a Net handle",
                )
            except ValueError as error:
                _raise_cross_design_with_context(error, self, "terminal marker")
            name = name_or_net
        return self._power_port(
            name,
            net=marker_net,
            at=at,
            orient=_terminal_marker_orientation(marker_kind, orient),
            kind=marker_kind,
            _authored_region=_authored_region,
        )

    def terminal_stub(
        self,
        name_or_net: str | Net | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        kind: str = "Power",
        side: str | None = None,
        length: float = 8,
        orient: str | None = None,
        _authored_region: int | None = None,
    ) -> SchematicTerminalStub:
        """Project a short existing-net wire ending in a terminal marker."""
        marker_kind = _terminal_marker_kind(kind)
        side_orientation = _signal_stub_side(side, at)
        if isinstance(name_or_net, Net):
            if net is not None:
                raise ValueError("Pass terminal marker net either first or as net=, not both")
            try:
                marker_net = _validate_explicit_schematic_net(
                    self._design,
                    name_or_net,
                    type_message="Schematic terminal markers expect a Net handle",
                )
            except ValueError as error:
                _raise_cross_design_with_context(error, self, "terminal stub")
            name = marker_net.name
        else:
            if name_or_net is not None and not isinstance(name_or_net, str):
                raise TypeError("Schematic terminal markers expect a name string, Net handle, or None")
            if isinstance(name_or_net, str) and not name_or_net:
                raise ValueError("Schematic terminal marker names must not be empty")
            try:
                marker_net = _validate_explicit_schematic_net(
                    self._design,
                    net,
                    type_message="Schematic terminal markers expect a Net handle",
                )
            except ValueError as error:
                _raise_cross_design_with_context(error, self, "terminal stub")
            name = name_or_net
        endpoint = _schematic_endpoint_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="terminal stub",
        )
        start = (endpoint[0], endpoint[1])
        end = _offset_schematic_point(
            start,
            side_orientation,
            _positive_coordinate(length, "Terminal stub lengths"),
        )
        port = self._power_port(
            name,
            net=marker_net,
            at=end,
            orient=_terminal_marker_orientation(marker_kind, orient),
            kind=marker_kind,
            _authored_region=_authored_region,
            _endpoint_payload=_retarget_schematic_endpoint(endpoint, end),
            action="terminal stub",
        )
        wire = self._add_wire_with_endpoint_payloads(
            port.net,
            (start, end),
            (endpoint, (end[0], end[1], None, None)),
            route_intent="Direct",
            _authored_region=_authored_region,
            action="terminal stub",
        )
        return SchematicTerminalStub(
            self,
            net=port.net,
            side=side_orientation,
            wire=wire,
            port=port,
            start=start,
            end=end,
        )

    def power(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Up",
        _authored_region: int | None = None,
    ) -> SchematicPort:
        """Place a power marker symbol on an existing net.

        Convenience wrapper for terminal(name, kind="Power", ...).

        Args:
            name: Display label for the power marker.
            at: Placement anchor. Pin or port anchors can infer the net if not provided.
            net: Explicit net binding. Required for coordinate anchors.
            orient: Marker orientation. Defaults to "Up".

        Returns:
            SchematicPort handle to the placed power marker.
        """
        return self.terminal(
            name,
            at=at,
            net=net,
            kind="Power",
            orient=orient,
            _authored_region=_authored_region,
        )

    def power_stub(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        side: str = "Up",
        length: float = 8,
        orient: str = "Up",
        _authored_region: int | None = None,
    ) -> SchematicTerminalStub:
        """Project a short existing-net wire ending in a power marker."""
        return self.terminal_stub(
            name,
            at=at,
            net=net,
            kind="Power",
            side=side,
            length=length,
            orient=orient,
            _authored_region=_authored_region,
        )

    def ground(
        self,
        name: str | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Down",
        _authored_region: int | None = None,
    ) -> SchematicPort:
        """Place a ground marker symbol on an existing net.

        Convenience wrapper for terminal(name, kind="Ground", ...).

        Args:
            name: Display label for the ground marker. If None, uses the net name.
            at: Placement anchor. Pin or port anchors can infer the net if not provided.
            net: Explicit net binding. Required for coordinate anchors.
            orient: Marker orientation. Defaults to "Down".

        Returns:
            SchematicPort handle to the placed ground marker.
        """
        return self.terminal(
            name,
            at=at,
            net=net,
            kind="Ground",
            orient=orient,
            _authored_region=_authored_region,
        )

    def ground_stub(
        self,
        name: str | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        side: str = "Down",
        length: float = 8,
        orient: str = "Down",
        _authored_region: int | None = None,
    ) -> SchematicTerminalStub:
        """Project a short existing-net wire ending in a ground marker."""
        return self.terminal_stub(
            name,
            at=at,
            net=net,
            kind="Ground",
            side=side,
            length=length,
            orient=orient,
            _authored_region=_authored_region,
        )

    def _power_port(
        self,
        name: str | None,
        *,
        net: Net | None,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str,
        kind: str,
        _authored_region: int | None = None,
        _endpoint_payload: tuple[float, float, int | None, int | None] | None = None,
        action: str = "terminal marker",
    ) -> SchematicPort:
        if name is not None and not isinstance(name, str):
            raise TypeError("Schematic terminal marker names must be strings")
        if name == "":
            raise ValueError("Schematic terminal marker names must not be empty")
        if net is not None:
            _require_schematic_net(
                net,
                self._design,
                type_message="Schematic terminal markers expect a Net handle",
            )
        endpoint = _endpoint_payload or _schematic_endpoint_for_authoring(
            at, design=self._design, schematic=self, action=action
        )
        orientation = _orientation(orient)
        try:
            port, resolved_net_index = (
                self._design._circuit.add_schematic_terminal_marker_for_endpoint(
                    self._sheet_index,
                    None if net is None else net.index,
                    kind,
                    endpoint,
                    orientation,
                    _authored_region,
                    name,
                )
            )
        except ValueError as error:
            raise ValueError(_with_schematic_context(str(error), self, action)) from error
        resolved_net = (
            net if net is not None else _net_by_index(self._design, resolved_net_index)
        )
        display_name = resolved_net.name if name is None or name == resolved_net.name else name
        return SchematicPort(
            self,
            port,
            net=resolved_net,
            name=display_name,
            kind=kind,
            at=(endpoint[0], endpoint[1]),
            orientation=orientation,
        )

    def no_connect(
        self,
        pin: SchematicPinAnchor,
        *,
        orient: str = "Right",
        offset: float = 0,
        reason: str | None = None,
        _authored_region: int | None = None,
    ) -> SchematicNoConnect:
        if not isinstance(pin, SchematicPinAnchor):
            raise TypeError("Schematic no-connect markers expect a placed pin anchor")
        if reason is not None and not isinstance(reason, str):
            raise TypeError("Schematic no-connect reasons must be strings")
        _require_schematic_point_design_for_authoring(
            pin,
            self._design,
            schematic=self,
            action="no-connect marker",
        )
        orientation = _orientation(orient)
        marker_x, marker_y = _offset_schematic_point(
            (pin.x, pin.y),
            orientation,
            _nonnegative_coordinate(offset, "No-connect marker offsets"),
        )
        marker = self._design._circuit.add_schematic_no_connect_marker(
            self._sheet_index,
            pin.pin.index,
            marker_x,
            marker_y,
            orientation,
            reason or "",
            _authored_region,
        )
        return SchematicNoConnect(self, marker, pin.pin)

    def sheet_port(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        kind: str = "Bidirectional",
        orient: str = "Right",
        _authored_region: int | None = None,
    ) -> SchematicPort:
        if not isinstance(name, str):
            raise TypeError("Schematic sheet port names must be strings")
        if not name:
            raise ValueError("Schematic sheet port names must not be empty")
        if net is None and not isinstance(at, (SchematicPinAnchor, SchematicPort)):
            net = _net_by_name(self._design, name, context="Schematic sheet ports")
        else:
            try:
                net = _validate_explicit_schematic_net(
                    self._design,
                    net,
                    type_message="Schematic sheet ports expect a Net handle",
                )
            except ValueError as error:
                _raise_cross_design_with_context(error, self, "sheet port")
        endpoint = _schematic_endpoint_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="sheet port",
        )
        orientation = _orientation(orient)
        port_kind = _sheet_port_kind(kind)
        try:
            port, resolved_net_index = self._design._circuit.add_schematic_sheet_port_for_endpoint(
                self._sheet_index,
                None if net is None else net.index,
                name,
                port_kind,
                endpoint,
                orientation,
                _authored_region,
            )
        except ValueError as error:
            raise ValueError(_with_schematic_context(str(error), self, "sheet port")) from error
        resolved_net = (
            net if net is not None else _net_by_index(self._design, resolved_net_index)
        )
        return SchematicPort(
            self,
            port,
            net=resolved_net,
            name=name,
            kind=port_kind,
            at=(endpoint[0], endpoint[1]),
            orientation=orientation,
        )

    def off_page(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Right",
        _authored_region: int | None = None,
    ) -> SchematicPort:
        return self.sheet_port(
            name,
            at=at,
            net=net,
            kind="OffPage",
            orient=orient,
            _authored_region=_authored_region,
        )

    def to_json(self) -> str:
        return self._design._circuit.schematic_to_json()

    def to_svg(self) -> str:
        return self._design._circuit.schematic_to_svg()

    def to_body_svg(self, *, margin: float = 4.0) -> str:
        """Return a content-tight SVG for this sheet's schematic body."""
        return self._design._circuit.schematic_to_body_svg(
            self._sheet_index,
            _nonnegative_coordinate(margin, "Schematic SVG body margins"),
        )

    def to_svg_pages(self) -> tuple[dict[str, int | str], ...]:
        return tuple(
            {
                "sheet": int(page["sheet"]),
                "name": str(page["name"]),
                "svg": str(page["svg"]),
            }
            for page in self._design._circuit.schematic_svg_pages()
        )

    def validate(self) -> DiagnosticReport:
        return DiagnosticReport(
            _diagnostic_from_dict(item)
            for item in self._design._circuit.validate_schematic()
        )

    def validate_readability(self) -> DiagnosticReport:
        return DiagnosticReport(
            _diagnostic_from_dict(item)
            for item in self._design._circuit.validate_schematic_readability()
        )

    def write_svg(self, path: str | Path) -> None:
        Path(path).write_text(self.to_svg(), encoding="utf-8")

    def write_body_svg(self, path: str | Path, *, margin: float = 4.0) -> None:
        Path(path).write_text(self.to_body_svg(margin=margin), encoding="utf-8")

    def write_svg_pages(
        self, directory: str | Path, *, prefix: str | None = None
    ) -> tuple[Path, ...]:
        if prefix is not None and not isinstance(prefix, str):
            raise TypeError("Schematic SVG page prefixes must be strings")
        if prefix is not None and ("/" in prefix or "\\" in prefix):
            raise ValueError("Schematic SVG page prefixes must not contain path separators")
        target = Path(directory)
        target.mkdir(parents=True, exist_ok=True)

        paths = []
        used_names = set()
        for page in self.to_svg_pages():
            stem = _schematic_svg_page_filename(page["name"])
            if prefix:
                stem = f"{prefix}_{stem}"
            filename = f"{stem}.svg"
            if filename in used_names:
                filename = f"{stem}_sheet_{page['sheet']}.svg"
            used_names.add(filename)

            path = target / filename
            path.write_text(str(page["svg"]), encoding="utf-8")
            paths.append(path)
        return tuple(paths)

    def write_json(self, path: str | Path) -> None:
        Path(path).write_text(self.to_json(), encoding="utf-8")

    def __repr__(self) -> str:
        return f"Schematic(name={self.name!r}, sheet_index={self._sheet_index})"


class SchematicRegion:
    """Presentation-only authoring surface for a named region on one schematic sheet."""

    def __init__(
        self,
        sheet: Schematic,
        index: int,
        *,
        name: str,
        title: str,
        bounds: tuple[float, float, float, float],
        style: dict[str, str],
    ):
        self._sheet = sheet
        self._design = sheet._design
        self._sheet_index = sheet.sheet_index
        self._index = index
        self.name = name
        self.title = title
        self.bounds = bounds
        self.style = dict(style)

    @property
    def index(self) -> int:
        return self._index

    @property
    def origin(self) -> tuple[float, float]:
        return (self.bounds[0], self.bounds[1])

    def drawing(
        self,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort = (0, 0),
        direction: str = "Right",
        unit: float = 20,
    ) -> SchematicDrawing:
        return SchematicDrawing(
            self._sheet,
            at=at,
            direction=direction,
            unit=unit,
            coordinate_origin=self.origin,
            authored_region=self._index,
        )

    def place(
        self,
        component: Component,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        reference_label: str | None = None,
    ) -> SchematicSymbol:
        return self._sheet.place(
            component,
            at=self._local_point(at),
            orient=orient,
            symbol=symbol,
            variant=variant,
            reference_label=reference_label,
            _authored_region=self._index,
        )

    def wire(
        self,
        net: Net,
        points: Iterable[tuple[float, float] | SchematicAnchor | SchematicPort],
    ) -> SchematicWire:
        return self._sheet.wire(
            net,
            tuple(self._local_point(point) for point in points),
            _authored_region=self._index,
        )

    def ortho_lines(
        self,
        entries,
        *,
        shape: str | None = None,
        k: float | None = None,
    ) -> tuple[SchematicWire, ...]:
        localized = []
        for entry in entries:
            net, start, end = _schematic_ortho_line_entry_parts(entry)
            localized.append((net, self._local_point(start), self._local_point(end)))
        return self._sheet.ortho_lines(
            localized,
            shape=shape,
            k=k,
            _authored_region=self._index,
        )

    def label(
        self,
        net: Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
        label: str | None = None,
    ) -> SchematicNetLabel:
        return self._sheet.label(
            net,
            at=self._local_point(at),
            orient=orient,
            label=label,
            _authored_region=self._index,
        )

    def net_label(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
        label: str | None = None,
    ) -> SchematicNetLabel:
        return self._sheet.net_label(
            name_or_net,
            at=self._local_point(at),
            orient=orient,
            label=label,
            _authored_region=self._index,
        )

    def local_label(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        side: str = "Right",
        offset: float = 2,
        orient: str | None = None,
    ) -> SchematicNetLabel:
        return self._sheet.local_label(
            name_or_net,
            at=self._local_point(at),
            side=side,
            offset=offset,
            orient=orient,
            _authored_region=self._index,
        )

    def signal_stub(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        side: str | None = None,
        length: float = 8,
        label_gap: float = 2,
        orient: str | None = None,
        label: str | None = None,
    ) -> SchematicSignalStub:
        return self._sheet.signal_stub(
            name_or_net,
            at=self._local_point(at),
            side=side,
            length=length,
            label_gap=label_gap,
            orient=orient,
            label=label,
            _authored_region=self._index,
        )

    def signal_tag(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        side: str | None = None,
        length: float = 8,
        label: str | None = None,
        kind: str = "Bidirectional",
        orient: str | None = None,
    ) -> SchematicSignalTag:
        return self._sheet.signal_tag(
            name_or_net,
            at=self._local_point(at),
            side=side,
            length=length,
            label=label,
            kind=kind,
            orient=orient,
            _authored_region=self._index,
        )

    def signal_tags(
        self,
        items,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        side: str | None = None,
        pitch: float = 8,
        length: float = 8,
        kind: str = "Bidirectional",
        orient: str | None = None,
    ) -> tuple[SchematicSignalTag, ...]:
        base_at = None if at is None else self._local_point(at)
        localized = (self._local_signal_stub_item(item) for item in items)
        return self._sheet.signal_tags(
            localized,
            at=base_at,
            side=side,
            pitch=pitch,
            length=length,
            kind=kind,
            orient=orient,
            _authored_region=self._index,
        )

    def signal_stubs(
        self,
        items,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        side: str | None = None,
        pitch: float = 8,
        length: float = 8,
        label_gap: float = 2,
        orient: str | None = None,
    ) -> tuple[SchematicSignalStub, ...]:
        base_at = None if at is None else self._local_point(at)
        localized = (self._local_signal_stub_item(item) for item in items)
        return self._sheet.signal_stubs(
            localized,
            at=base_at,
            side=side,
            pitch=pitch,
            length=length,
            label_gap=label_gap,
            orient=orient,
            _authored_region=self._index,
        )

    def _local_signal_stub_item(self, item):
        name_or_net, anchor, label = _signal_stub_entry_parts(item)
        if anchor is None:
            if label is None:
                return item
            return name_or_net, None, label
        localized = self._local_point(anchor)
        if label is None:
            return name_or_net, localized
        return name_or_net, localized, label

    def junction(
        self,
        net: Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
    ) -> SchematicJunction:
        return self._sheet.junction(
            net, at=self._local_point(at), _authored_region=self._index
        )

    def terminal(
        self,
        name_or_net: str | Net | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        kind: str = "Power",
        orient: str | None = None,
    ) -> SchematicPort:
        """Place a generic one-terminal marker in this region.

        See Schematic.terminal() for full documentation.
        Coordinates are interpreted relative to this region's bounds.
        """
        return self._sheet.terminal(
            name_or_net,
            at=self._local_point(at),
            net=net,
            kind=kind,
            orient=orient,
            _authored_region=self._index,
        )

    def terminal_stub(
        self,
        name_or_net: str | Net | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        kind: str = "Power",
        side: str | None = None,
        length: float = 8,
        orient: str | None = None,
    ) -> SchematicTerminalStub:
        """Draw a short region-local wire to a terminal marker."""
        return self._sheet.terminal_stub(
            name_or_net,
            at=self._local_point(at),
            net=net,
            kind=kind,
            side=side,
            length=length,
            orient=orient,
            _authored_region=self._index,
        )

    def power(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Up",
    ) -> SchematicPort:
        """Place a power marker in this region.

        See Schematic.power() for full documentation.
        Coordinates are interpreted relative to this region's bounds.
        """
        return self._sheet.power(
            name,
            at=self._local_point(at),
            net=net,
            orient=orient,
            _authored_region=self._index,
        )

    def power_stub(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        side: str = "Up",
        length: float = 8,
        orient: str = "Up",
    ) -> SchematicTerminalStub:
        """Draw a short region-local wire to a power marker."""
        return self._sheet.power_stub(
            name,
            at=self._local_point(at),
            net=net,
            side=side,
            length=length,
            orient=orient,
            _authored_region=self._index,
        )

    def ground(
        self,
        name: str | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Down",
    ) -> SchematicPort:
        """Place a ground marker in this region.

        See Schematic.ground() for full documentation.
        Coordinates are interpreted relative to this region's bounds.
        """
        return self._sheet.ground(
            name,
            at=self._local_point(at),
            net=net,
            orient=orient,
            _authored_region=self._index,
        )

    def ground_stub(
        self,
        name: str | None = None,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        side: str = "Down",
        length: float = 8,
        orient: str = "Down",
    ) -> SchematicTerminalStub:
        """Draw a short region-local wire to a ground marker."""
        return self._sheet.ground_stub(
            name,
            at=self._local_point(at),
            net=net,
            side=side,
            length=length,
            orient=orient,
            _authored_region=self._index,
        )

    def sheet_port(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        kind: str = "Bidirectional",
        orient: str = "Right",
    ) -> SchematicPort:
        return self._sheet.sheet_port(
            name,
            at=self._local_point(at),
            net=net,
            kind=kind,
            orient=orient,
            _authored_region=self._index,
        )

    def off_page(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Right",
    ) -> SchematicPort:
        return self._sheet.off_page(
            name,
            at=self._local_point(at),
            net=net,
            orient=orient,
            _authored_region=self._index,
        )

    def no_connect(
        self,
        anchor: SchematicPinAnchor,
        *,
        orient: str = "Right",
        offset: float = 0,
        reason: str | None = None,
    ) -> SchematicNoConnect:
        return self._sheet.no_connect(
            anchor,
            orient=orient,
            offset=offset,
            reason=reason,
            _authored_region=self._index,
        )

    def _local_point(
        self, value: tuple[float, float] | SchematicAnchor | SchematicPort
    ) -> tuple[float, float] | SchematicAnchor | SchematicPort:
        if isinstance(value, (tuple, list)):
            x, y = _schematic_point_tuple(value)
            return (self.bounds[0] + x, self.bounds[1] + y)
        return value

    def __repr__(self) -> str:
        return (
            f"SchematicRegion(name={self.name!r}, sheet={self._sheet.name!r}, "
            f"index={self._index})"
        )

def _schematic_point_tuple(value) -> tuple[float, float]:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise TypeError("Schematic points must be anchors, ports, or (x, y) pairs")
    return (_coordinate(value[0]), _coordinate(value[1]))


def _schematic_sheet_phrase(schematic: Schematic) -> str:
    return f"sheet {schematic.name!r}"


def _schematic_authoring_context(schematic: Schematic, action: str) -> str:
    return f"{action} on {_schematic_sheet_phrase(schematic)}"


def _with_schematic_context(message: str, schematic: Schematic, action: str) -> str:
    return f"{message} while authoring {_schematic_authoring_context(schematic, action)}"


def _cross_design_anchor_message(value) -> str:
    if isinstance(value, SchematicPinAnchor):
        subject = _pin_anchor_label(value)
    elif isinstance(value, SchematicPort):
        subject = f"schematic port {value.name!r} for {_net_label(value.net)}"
    elif isinstance(value, SchematicAnchor):
        subject = f"schematic anchor at {value.point!r}"
    else:
        subject = "Schematic anchor"
    return f"{subject} belongs to a different design"


def _cross_design_net_message(net: Net) -> str:
    return f"{_net_label(net)} belongs to a different design"


def _raise_cross_design_with_context(error: ValueError, schematic: Schematic, action: str) -> None:
    message = str(error)
    if "different design" in message:
        raise ValueError(_with_schematic_context(message, schematic, action)) from error
    raise error


def _require_schematic_point_design(value, design: Design) -> None:
    point_design = getattr(value, "_design", None)
    if point_design is not None and point_design is not design:
        raise ValueError(_cross_design_anchor_message(value))


def _require_schematic_point_design_for_authoring(
    value, design: Design, *, schematic: Schematic, action: str
) -> None:
    try:
        _require_schematic_point_design(value, design)
    except ValueError as error:
        _raise_cross_design_with_context(error, schematic, action)


def _schematic_point(value, *, design: Design) -> tuple[float, float]:
    if isinstance(value, SchematicPort):
        if value.net._design is not design:
            raise ValueError(_cross_design_anchor_message(value))
        return value.pin.point
    if isinstance(value, SchematicAnchor):
        _require_schematic_point_design(value, design)
        return value.point
    return _schematic_point_tuple(value)


def _schematic_point_for_authoring(
    value, *, design: Design, schematic: Schematic, action: str
) -> tuple[float, float]:
    try:
        package = sys.modules.get(__package__)
        point_func = getattr(package, "_schematic_point", _schematic_point)
        return point_func(value, design=design)
    except ValueError as error:
        _raise_cross_design_with_context(error, schematic, action)


def _schematic_endpoint_for_authoring(
    value, *, design: Design, schematic: Schematic, action: str
) -> tuple[float, float, int | None, int | None]:
    point = _schematic_point_for_authoring(
        value,
        design=design,
        schematic=schematic,
        action=action,
    )
    pin_index = value.pin.index if isinstance(value, SchematicPinAnchor) else None
    port_net_index = value.net.index if isinstance(value, SchematicPort) else None
    return (point[0], point[1], pin_index, port_net_index)


def _retarget_schematic_endpoint(
    endpoint: tuple[float, float, int | None, int | None],
    point: tuple[float, float],
) -> tuple[float, float, int | None, int | None]:
    x, y = _schematic_point_tuple(point)
    return (x, y, endpoint[2], endpoint[3])


def _require_schematic_net(
    net: Net,
    design: Design,
    *,
    type_message: str,
) -> None:
    if not isinstance(net, Net):
        raise TypeError(type_message)
    if net._design is not design:
        raise ValueError(_cross_design_net_message(net))


def _net_by_index(design: Design, index: int) -> Net:
    for net in design.nets():
        if net.index == index:
            return net
    raise ValueError(f"Kernel returned missing logical net net:{index}")


def _net_by_name(design: Design, name: str, *, context: str) -> Net:
    if not isinstance(name, str):
        raise TypeError(f"{context} names must be strings")
    if not name:
        raise ValueError(f"{context} names must not be empty")
    for net in design.nets():
        if net.name == name:
            return net
    raise ValueError(f"{context} require an existing logical net named {name!r}")


def _resolve_schematic_net_label(design: Design, value: str | Net) -> Net:
    if isinstance(value, Net):
        _require_schematic_net(
            value,
            design,
            type_message="Schematic net labels expect a Net handle or existing net name",
        )
        return value
    if isinstance(value, str):
        return _net_by_name(design, value, context="Schematic net labels")
    raise TypeError("Schematic net labels expect a Net handle or existing net name")


def _pin_anchor_label(anchor: SchematicPinAnchor) -> str:
    component_index = anchor.pin._design._circuit.pin_component(anchor.pin.index)
    component_reference = anchor.pin._design._circuit.component_reference(component_index)
    return f"{component_reference} pin {anchor.number} ({anchor.name})"


def _net_label(net: Net) -> str:
    return f"{net.name} (net:{net.index})"


def _optional_display_label(value: str | None) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str):
        raise TypeError("Schematic net label display text must be a string")
    if not value:
        raise ValueError("Schematic net label display text must not be empty")
    return value


def _missing_schematic_symbol_message(
    component: Component, schematic: Schematic, variant: str
) -> str:
    if variant == "default":
        symbol_label = "default schematic symbol"
    else:
        symbol_label = f"schematic symbol variant {variant!r}"
    return (
        f"No {symbol_label} for component {component.reference} on "
        f"{_schematic_sheet_phrase(schematic)}; pass symbol= for this placement "
        "or define schematic_symbol= on the component/library definition"
    )


def _validate_explicit_schematic_net(
    design: Design,
    net: Net | None,
    *,
    type_message: str,
) -> Net | None:
    if net is None:
        return None
    _require_schematic_net(net, design, type_message=type_message)
    return net


def _add_schematic_junction_dot(
    schematic: Schematic,
    at: tuple[float, float] | SchematicAnchor | SchematicPort,
    *,
    net: Net | None,
    _authored_region: int | None,
    action: str,
) -> SchematicJunction:
    try:
        explicit = _validate_explicit_schematic_net(
            schematic._design,
            net,
            type_message="Schematic junction dots expect a Net handle",
        )
    except ValueError as error:
        _raise_cross_design_with_context(error, schematic, action)
    endpoint = _schematic_endpoint_for_authoring(
        at,
        design=schematic._design,
        schematic=schematic,
        action=action,
    )
    try:
        junction, _resolved_net_index = (
            schematic._design._circuit.add_schematic_junction_for_endpoint(
                schematic._sheet_index,
                None if explicit is None else explicit.index,
                endpoint,
                _authored_region,
            )
        )
    except ValueError as error:
        raise ValueError(_with_schematic_context(str(error), schematic, action)) from error
    return SchematicJunction(schematic, junction)


def _terminal_marker_kind(kind: str) -> str:
    if not isinstance(kind, str):
        raise TypeError("Schematic terminal marker kinds must be strings")
    normalized = {
        "power": "Power",
        "ground": "Ground",
    }.get(kind.casefold())
    if normalized is None:
        raise ValueError("Schematic terminal marker kind must be Power or Ground")
    return normalized


def _terminal_marker_orientation(kind: str, orient: str | None) -> str:
    if orient is not None:
        return _orientation(orient)
    return "Down" if kind == "Ground" else "Up"


def _offset_schematic_point(
    point: tuple[float, float], orientation: str, distance: float
) -> tuple[float, float]:
    x, y = point
    if orientation == "Right":
        return (x + distance, y)
    if orientation == "Down":
        return (x, y + distance)
    if orientation == "Left":
        return (x - distance, y)
    if orientation == "Up":
        return (x, y - distance)
    raise ValueError("Schematic orientation must be Right, Down, Left, or Up")


def _signal_stub_side(
    side: str | None, at: tuple[float, float] | SchematicAnchor | SchematicPort
) -> str:
    if side is not None:
        return _orientation(side)
    if isinstance(at, (SchematicPinAnchor, SchematicPort)):
        return _orientation(at.orientation)
    return "Right"


def _signal_stub_entry_has_anchor(item) -> bool:
    return _signal_stub_entry_parts(item)[1] is not None


def _signal_stub_entry_parts(item):
    if isinstance(item, (tuple, list)):
        if len(item) == 2:
            if _is_schematic_authoring_anchor(item[1]):
                return item[0], item[1], None
            if item[1] is None or isinstance(item[1], str):
                return item[0], None, item[1]
            raise TypeError(
                "Signal stub entries must be bare nets/names, (net, anchor), "
                "(net, label), or (net, anchor, label)"
            )
        if len(item) == 3:
            if item[2] is not None and not isinstance(item[2], str):
                raise TypeError("Signal stub labels must be strings")
            if item[1] is None or _is_schematic_authoring_anchor(item[1]):
                return item[0], item[1], item[2]
            raise TypeError(
                "Signal stub entries must be bare nets/names, (net, anchor), "
                "(net, label), or (net, anchor, label)"
            )
        raise TypeError(
            "Signal stub entries must be bare nets/names, (net, anchor), "
            "(net, label), or (net, anchor, label)"
        )
    return item, None, None


def _is_schematic_authoring_anchor(value) -> bool:
    if isinstance(value, (SchematicAnchor, SchematicPort)):
        return True
    try:
        _schematic_point_tuple(value)
    except (TypeError, ValueError):
        return False
    return True


def _signal_stub_entries(
    items,
    *,
    at: tuple[float, float] | SchematicAnchor | SchematicPort | None,
    side: str,
    pitch: float,
):
    if at is None:
        for item in items:
            name_or_net, anchor, label = _signal_stub_entry_parts(item)
            if anchor is None:
                raise TypeError(
                    "Signal stub groups need (net, anchor) entries unless at= is provided"
                )
            yield name_or_net, anchor, label, False
        return

    base = at
    for index, item in enumerate(items):
        name_or_net, anchor, label = _signal_stub_entry_parts(item)
        if anchor is not None:
            yield name_or_net, anchor, label, False
            continue
        offset = _signal_stub_pitch_offset(side, pitch * index)
        if index == 0 and isinstance(base, (SchematicPinAnchor, SchematicPort)):
            yield name_or_net, base, label, True
        elif isinstance(base, SchematicAnchor):
            yield name_or_net, base.offset(dx=offset[0], dy=offset[1]), label, True
        elif isinstance(base, SchematicPort):
            yield name_or_net, base.pin.offset(dx=offset[0], dy=offset[1]), label, True
        else:
            point = _schematic_point_tuple(base)
            yield name_or_net, (point[0] + offset[0], point[1] + offset[1]), label, True


def _signal_stub_pitch_offset(side: str, distance: float) -> tuple[float, float]:
    if side in ("Right", "Left"):
        return (0.0, distance)
    return (distance, 0.0)


def _schematic_direction_offset(direction: str, distance: float) -> tuple[float, float]:
    if direction == "Right":
        return (distance, 0.0)
    if direction == "Left":
        return (-distance, 0.0)
    if direction == "Down":
        return (0.0, distance)
    return (0.0, -distance)


def _schematic_ortho_line_entry_parts(item):
    if not isinstance(item, (tuple, list)):
        raise TypeError("Ortho line entries must be (start, end) or (net, start, end)")
    if len(item) == 2:
        return None, item[0], item[1]
    if len(item) == 3:
        net, start, end = item
        if net is not None and not isinstance(net, Net):
            raise TypeError("Ortho line explicit nets must be Net handles")
        return net, start, end
    raise TypeError("Ortho line entries must be (start, end) or (net, start, end)")

def _label_offset(offset: float | None, ofst: float | None, *, default: float = 14) -> float:
    if offset is not None and ofst is not None:
        raise ValueError("Use either offset= or ofst= for schematic element labels")
    value = default if offset is None and ofst is None else offset if ofst is None else ofst
    return _coordinate(value)


def _element_label_offset(name: str) -> float:
    return 10


def _element_label_loc(
    element: PlacedSchematicElement, name: str, loc: str | None
) -> str:
    if loc is not None:
        return loc
    vertical = element.orientation in {"Up", "Down"}
    if name.casefold() == "value":
        return "right" if vertical else "bottom"
    return "left" if vertical else "top"


def _element_label_point(
    element: PlacedSchematicElement, loc: str, offset: float
) -> SchematicAnchor:
    distance = _coordinate(offset)
    normalized = {
        "top": "top",
        "above": "top",
        "bottom": "bottom",
        "below": "bottom",
        "left": "left",
        "right": "right",
    }.get(loc.casefold() if isinstance(loc, str) else loc)
    if normalized is None:
        raise ValueError("Schematic element label loc must be top, bottom, left, or right")
    center = element.center
    anchors = element.pin_anchors()
    min_x = min(anchor.x for anchor in anchors)
    max_x = max(anchor.x for anchor in anchors)
    min_y = min(anchor.y for anchor in anchors)
    max_y = max(anchor.y for anchor in anchors)
    if normalized == "top":
        return SchematicAnchor((center.x, min_y - distance), design=center._design)
    if normalized == "bottom":
        return SchematicAnchor((center.x, max_y + distance), design=center._design)
    if normalized == "left":
        return SchematicAnchor((min_x - distance, center.y), design=center._design)
    return SchematicAnchor((max_x + distance, center.y), design=center._design)


def _sheet_port_kind(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic sheet port kind must be a string")
    normalized = {
        "input": "Input",
        "output": "Output",
        "bidirectional": "Bidirectional",
        "offpage": "OffPage",
        "off_page": "OffPage",
        "off-page": "OffPage",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError(
            "Schematic sheet port kind must be Input, Output, Bidirectional, or OffPage"
        )
    return normalized


def _resolve_schematic_symbol_pin_ref(pin_refs, key: int | str):
    if isinstance(key, int):
        matches = tuple(item for item in pin_refs if item["number"] == str(key))
        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            raise ValueError(f"Schematic symbol pin number {key!r} is ambiguous")
        raise IndexError("Schematic symbol has no pin with that number")

    if not isinstance(key, str):
        raise TypeError("Schematic symbol pins are addressed by int number or str name")

    name_matches = tuple(item for item in pin_refs if item["name"] == key)
    if len(name_matches) == 1:
        return name_matches[0]
    if len(name_matches) > 1:
        numbers = ", ".join(f"{item['number']!r}" for item in name_matches)
        raise ValueError(
            f"Schematic symbol pin name {key!r} is ambiguous; use pins({key!r}) "
            "for the group or address one physical pin by number. "
            f"Matching pin numbers: {numbers}"
        )

    number_matches = tuple(item for item in pin_refs if item["number"] == key)
    if len(number_matches) == 1:
        return number_matches[0]
    if len(number_matches) > 1:
        raise ValueError(f"Schematic symbol pin number {key!r} is ambiguous")

    raise IndexError("Schematic symbol has no pin with that name or number")


def _schematic_axis_target(
    value,
    design: Design,
    axis: str,
    *,
    schematic: Schematic,
    action: str,
) -> float:
    if isinstance(value, bool):
        raise TypeError("Schematic coordinates must be numbers")
    if isinstance(value, (int, float)):
        return _coordinate(value)
    point = _schematic_point_for_authoring(
        value, design=design, schematic=schematic, action=action
    )
    return point[0] if axis == "x" else point[1]


def _schematic_anchor_axis_target(value, design: Design | None, axis: str) -> float:
    if isinstance(value, bool):
        raise TypeError("Schematic coordinates must be numbers")
    if isinstance(value, (int, float)):
        return _coordinate(value)
    if design is not None:
        point = _schematic_point(value, design=design)
    elif isinstance(value, SchematicPort):
        point = value.pin.point
    elif isinstance(value, SchematicAnchor):
        point = value.point
    else:
        point = _schematic_point_tuple(value)
    return point[0] if axis == "x" else point[1]
