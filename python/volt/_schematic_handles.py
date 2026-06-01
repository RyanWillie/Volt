"""Handle objects returned by schematic authoring helpers."""

from __future__ import annotations

from typing import TYPE_CHECKING, Iterable

from ._schematic_labels import _component_value_label
from ._utils import _coordinate
from .logical import Component, Net, Pin, _pin_refs_by_name

if TYPE_CHECKING:
    from .design import Design
    from .schematic import Schematic


class SchematicAnchor:
    """Sheet point that can be offset directionally while preserving author intent."""

    def __init__(self, point: tuple[float, float], *, design: Design | None = None):
        self._point = _schematic_point_tuple(point)
        self._design = design

    @property
    def x(self) -> float:
        """Return the sheet x coordinate."""
        return self._point[0]

    @property
    def y(self) -> float:
        """Return the sheet y coordinate."""
        return self._point[1]

    @property
    def point(self) -> tuple[float, float]:
        """Return this anchor as an ``(x, y)`` tuple."""
        return self._point

    def offset(self, dx: float = 0, dy: float = 0) -> SchematicAnchor:
        """Return a new anchor offset from this point."""
        return SchematicAnchor(
            (self.x + _coordinate(dx), self.y + _coordinate(dy)),
            design=self._design,
        )

    def left(self, distance: float) -> SchematicAnchor:
        """Return a new anchor moved left by a distance."""
        return self.offset(dx=-_coordinate(distance))

    def right(self, distance: float) -> SchematicAnchor:
        """Return a new anchor moved right by a distance."""
        return self.offset(dx=_coordinate(distance))

    def up(self, distance: float) -> SchematicAnchor:
        """Return a new anchor moved up by a distance."""
        return self.offset(dy=-_coordinate(distance))

    def down(self, distance: float) -> SchematicAnchor:
        """Return a new anchor moved down by a distance."""
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
        """Iterate over the ``(x, y)`` coordinate values."""
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
        """Return the kernel index for this schematic port projection."""
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
        """Return the kernel index for this schematic junction."""
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
        """Return the kernel index for this no-connect marker."""
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
        """Return the kernel index for this placed schematic symbol."""
        return self._index

    @property
    def component(self) -> Component:
        """Return the logical component represented by this placed symbol."""
        if self._component is None:
            raise ValueError("Placed symbol component is not available")
        return self._component

    @property
    def orientation(self) -> str:
        """Return the placed symbol orientation."""
        if self._orientation is None:
            return self._schematic._design._circuit.schematic_symbol_orientation(self._index)
        return self._orientation

    def pin_anchor(self, number: int | str) -> tuple[float, float]:
        """Return the sheet coordinate for a symbol pin number."""
        if not isinstance(number, (int, str)):
            raise TypeError("pin_anchor expects a pin number")
        return self._schematic._design._circuit.schematic_symbol_pin_anchor(
            self._index, str(number)
        )

    def pin(self, key: int | str) -> SchematicPinAnchor:
        """Return a placed symbol pin anchor by pin name or number."""
        try:
            pin_ref = _resolve_schematic_symbol_pin_ref(self._pin_refs(), key)
        except ValueError as error:
            if "ambiguous" in str(error):
                raise ValueError(f"{error} for {self._pin_context()}") from error
            raise
        return self._pin_anchor_for_ref(pin_ref)

    def pins(self, name: str) -> tuple[SchematicPinAnchor, ...]:
        """Return every placed symbol pin anchor with the given pin name."""
        if not isinstance(name, str):
            raise TypeError("Schematic symbol pin groups are addressed by str name")
        matches = tuple(item for item in self._pin_refs() if item["name"] == name)
        if not matches:
            raise IndexError("Schematic symbol has no pin with that name")
        return tuple(self._pin_anchor_for_ref(item) for item in matches)

    def pin_anchors(self) -> tuple[SchematicPinAnchor, ...]:
        """Return all placed symbol pin anchors."""
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
        """Return the kernel index for the wrapped schematic symbol."""
        return self.symbol.index

    @property
    def component(self) -> Component:
        """Return the logical component represented by this element."""
        return self.symbol.component

    @property
    def orientation(self) -> str:
        """Return this element's placed orientation."""
        return self.symbol.orientation

    @property
    def start(self) -> SchematicPinAnchor:
        """Return the first terminal anchor for this placed element."""
        return self._terminal_anchor(0, "start")

    @property
    def end(self) -> SchematicPinAnchor:
        """Return the last terminal anchor for this placed element."""
        return self._terminal_anchor(-1, "end")

    @property
    def center(self) -> SchematicAnchor:
        """Return the center point of this element's placed pin anchors."""
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
        """Return a placed element pin anchor by pin name or number."""
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
        """Return the sheet coordinate for one placed element pin number."""
        return self.symbol.pin_anchor(number)

    def pin(self, key: int | str) -> SchematicPinAnchor:
        """Return a placed element pin anchor by pin name or number."""
        return self.symbol.pin(key)

    def pins(self, name: str) -> tuple[SchematicPinAnchor, ...]:
        """Return every placed element pin anchor with the given pin name."""
        return self.symbol.pins(name)

    def pin_anchors(self) -> tuple[SchematicPinAnchor, ...]:
        """Return all placed element pin anchors."""
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
        """Add a text field near this placed schematic element."""
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
        """Add a reference-designator label near this placed element."""
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
        """Add a value label near this placed element."""
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
        """Add a junction dot at this element's end terminal."""
        from .schematic import _add_schematic_junction_dot

        _add_schematic_junction_dot(
            self.symbol._schematic,
            self.end,
            net=net,
            _authored_region=self.symbol._authored_region,
            action="element endpoint dot",
        )
        return self

    def idot(self, *, net: Net | None = None) -> PlacedSchematicElement:
        """Add a junction dot at this element's start terminal."""
        from .schematic import _add_schematic_junction_dot

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
        """Return the kernel index for this schematic wire."""
        return self._index

    def dot(self) -> SchematicWire:
        """Add a junction dot at this wire's final point."""
        if not self._dot_end:
            from .schematic import _add_schematic_junction_dot

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
        """Add a junction dot at this wire's first point."""
        if not self._dot_start:
            from .schematic import _add_schematic_junction_dot

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
        """Return the kernel index for this schematic net label."""
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
        """Return the kernel index for this schematic symbol field."""
        return self._index

    def __repr__(self) -> str:
        return f"SchematicSymbolField(name={self.name!r}, index={self._index})"


def _schematic_point_tuple(value) -> tuple[float, float]:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise TypeError("Schematic points must be anchors, ports, or (x, y) pairs")
    return (_coordinate(value[0]), _coordinate(value[1]))


def _schematic_sheet_phrase(schematic: Schematic) -> str:
    return f"sheet {schematic.name!r}"


def _label_offset(offset: float | None, ofst: float | None, *, default: float = 14) -> float:
    if offset is not None and ofst is not None:
        raise ValueError("Use either offset= or ofst= for schematic element labels")
    value = default if offset is None and ofst is None else offset if ofst is None else ofst
    return _coordinate(value)


def _element_label_offset(name: str) -> float:
    return 10


def _element_label_loc(element: PlacedSchematicElement, name: str, loc: str | None) -> str:
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


def _schematic_anchor_axis_target(value, design: Design | None, axis: str) -> float:
    if isinstance(value, bool):
        raise TypeError("Schematic coordinates must be numbers")
    if isinstance(value, (int, float)):
        return _coordinate(value)
    if design is not None:
        from .schematic import _schematic_point

        point = _schematic_point(value, design=design)
    elif isinstance(value, SchematicPort):
        point = value.pin.point
    elif isinstance(value, SchematicAnchor):
        point = value.point
    else:
        point = _schematic_point_tuple(value)
    return point[0] if axis == "x" else point[1]
