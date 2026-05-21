"""Schematic symbol presentation helpers."""

from __future__ import annotations

from ._utils import _coordinate, _orientation
from .library import SchematicSymbolPinSpec, SchematicSymbolSpec, _default_two_terminal_symbol_spec


def _symbol_pin_dict(pin: SchematicSymbolPinSpec) -> dict:
    return {
        "name": pin.name,
        "number": str(pin.number),
        "at": pin.at,
        "orientation": _orientation(pin.orientation),
    }


def _symbol_terminal_frame(symbol: SchematicSymbolSpec) -> tuple[tuple[float, float], float]:
    pins = tuple(symbol.pins)
    if len(pins) != 2:
        raise ValueError("Two-terminal placement requires exactly two symbol pins")
    start = pins[0].at
    end = pins[-1].at
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    distance = (dx * dx + dy * dy) ** 0.5
    if distance <= 0:
        raise ValueError("Two-terminal symbol pins must not overlap")
    return (start, distance)


def _presentation_symbol_pins(
    symbol: SchematicSymbolSpec,
    *,
    length: float,
    reverse: bool,
    flip: bool,
) -> tuple[dict, ...]:
    pins = tuple(_symbol_pin_dict(pin) for pin in symbol.pins)
    transformed = []
    for pin in pins:
        transformed.append(
            {
                "name": pin["name"],
                "number": pin["number"],
                "at": _presentation_point(symbol, pin["at"], length, reverse, flip),
                "orientation": _presentation_orientation(pin["orientation"], reverse, flip),
            }
        )
    if reverse:
        transformed.reverse()
    return tuple(transformed)


def _needs_generated_two_terminal_symbol(
    symbol: SchematicSymbolSpec,
    *,
    length: float,
    reverse: bool,
    flip: bool,
) -> bool:
    _start, base_length = _symbol_terminal_frame(symbol)
    return reverse or flip or length != base_length


def _presentation_symbol_spec(
    symbol: SchematicSymbolSpec,
    *,
    length: float,
    reverse: bool,
    flip: bool,
) -> SchematicSymbolSpec:
    pins = tuple(
        SchematicSymbolSpec.pin(
            pin["name"], pin["number"], pin["at"], pin["orientation"]
        )
        for pin in _presentation_symbol_pins(
            symbol, length=length, reverse=reverse, flip=flip
        )
    )
    primitives = tuple(
        _presentation_primitive(symbol, primitive, length, reverse, flip)
        for primitive in symbol.primitives
    )
    return SchematicSymbolSpec(
        _presentation_symbol_name(symbol, length, reverse, flip),
        pins=pins,
        primitives=primitives,
    )


def _presentation_symbol_name(
    symbol: SchematicSymbolSpec, length: float, reverse: bool, flip: bool
) -> str:
    length_token = f"{length:g}".replace("-", "m").replace(".", "p")
    flags = []
    if reverse:
        flags.append("reverse")
    if flip:
        flags.append("flip")
    if flags:
        flag_token = "-".join(flags)
    elif _symbol_has_terminal_leads(symbol):
        flag_token = "span"
    else:
        flag_token = "scaled"
    return f"{symbol.name}#two-terminal-{flag_token}-{length_token}"


def _presentation_primitive(
    symbol: SchematicSymbolSpec,
    primitive: dict,
    length: float,
    reverse: bool,
    flip: bool,
) -> dict:
    if _symbol_has_terminal_leads(symbol):
        return _lead_span_presentation_primitive(symbol, primitive, length, reverse, flip)

    primitive_type = primitive["type"]
    if primitive_type == "line":
        return SchematicSymbolSpec.line(
            _presentation_point_dict(symbol, primitive["start"], length, reverse, flip),
            _presentation_point_dict(symbol, primitive["end"], length, reverse, flip),
        )
    if primitive_type == "rectangle":
        return SchematicSymbolSpec.rectangle(
            _presentation_point_dict(
                symbol, primitive["first_corner"], length, reverse, flip
            ),
            _presentation_point_dict(
                symbol, primitive["second_corner"], length, reverse, flip
            ),
        )
    if primitive_type == "circle":
        return SchematicSymbolSpec.circle(
            _presentation_point_dict(symbol, primitive["center"], length, reverse, flip),
            primitive["radius"],
        )
    if primitive_type == "arc":
        return SchematicSymbolSpec.arc(
            _presentation_point_dict(symbol, primitive["center"], length, reverse, flip),
            primitive["radius"],
            primitive["start_degrees"],
            primitive["sweep_degrees"],
        )
    if primitive_type == "text":
        return SchematicSymbolSpec.text(
            primitive["text"],
            _presentation_point_dict(symbol, primitive["anchor"], length, reverse, flip),
            _presentation_orientation(primitive["orientation"], reverse, flip),
        )
    raise ValueError(f"Unknown schematic symbol primitive type: {primitive_type!r}")


def _lead_span_presentation_primitive(
    symbol: SchematicSymbolSpec,
    primitive: dict,
    length: float,
    reverse: bool,
    flip: bool,
) -> dict:
    primitive_type = primitive["type"]
    if primitive_type == "line":
        terminal_role = _terminal_lead_role(primitive)
        if terminal_role is not None:
            return _presentation_terminal_lead_line(
                symbol, primitive, terminal_role, length, reverse, flip
            )
        return SchematicSymbolSpec.line(
            _lead_span_point_dict(symbol, primitive["start"], length, reverse, flip),
            _lead_span_point_dict(symbol, primitive["end"], length, reverse, flip),
        )
    if primitive_type == "rectangle":
        return SchematicSymbolSpec.rectangle(
            _lead_span_point_dict(symbol, primitive["first_corner"], length, reverse, flip),
            _lead_span_point_dict(symbol, primitive["second_corner"], length, reverse, flip),
        )
    if primitive_type == "circle":
        return SchematicSymbolSpec.circle(
            _lead_span_point_dict(symbol, primitive["center"], length, reverse, flip),
            primitive["radius"],
        )
    if primitive_type == "arc":
        return SchematicSymbolSpec.arc(
            _lead_span_point_dict(symbol, primitive["center"], length, reverse, flip),
            primitive["radius"],
            primitive["start_degrees"],
            primitive["sweep_degrees"],
        )
    if primitive_type == "text":
        return SchematicSymbolSpec.text(
            primitive["text"],
            _lead_span_point_dict(symbol, primitive["anchor"], length, reverse, flip),
            _presentation_orientation(primitive["orientation"], reverse, flip),
        )
    raise ValueError(f"Unknown schematic symbol primitive type: {primitive_type!r}")


def _symbol_has_terminal_leads(symbol: SchematicSymbolSpec) -> bool:
    roles = {_terminal_lead_role(primitive) for primitive in symbol.primitives}
    return "TerminalLeadStart" in roles and "TerminalLeadEnd" in roles


def _terminal_lead_role(primitive: dict) -> str | None:
    if primitive.get("type") != "line":
        return None
    role = primitive.get("role")
    if role in ("TerminalLeadStart", "TerminalLeadEnd"):
        return role
    return None


def _presentation_terminal_lead_line(
    symbol: SchematicSymbolSpec,
    primitive: dict,
    terminal_role: str,
    length: float,
    reverse: bool,
    flip: bool,
) -> dict:
    start_point = (primitive["start"]["x"], primitive["start"]["y"])
    end_point = (primitive["end"]["x"], primitive["end"]["y"])
    terminal_endpoint = _terminal_lead_endpoint(symbol, terminal_role, start_point, end_point)
    if terminal_endpoint == "start":
        return SchematicSymbolSpec.line(
            _presentation_terminal_lead_point(terminal_role, length, reverse),
            _lead_span_point(symbol, end_point, length, reverse, flip),
            role=terminal_role,
        )
    return SchematicSymbolSpec.line(
        _lead_span_point(symbol, start_point, length, reverse, flip),
        _presentation_terminal_lead_point(terminal_role, length, reverse),
        role=terminal_role,
    )


def _terminal_lead_endpoint(
    symbol: SchematicSymbolSpec,
    terminal_role: str,
    start_point: tuple[float, float],
    end_point: tuple[float, float],
) -> str:
    pins = tuple(symbol.pins)
    terminal = pins[0].at if terminal_role == "TerminalLeadStart" else pins[-1].at
    if _distance_squared(start_point, terminal) <= _distance_squared(end_point, terminal):
        return "start"
    return "end"


def _presentation_terminal_lead_point(
    terminal_role: str,
    length: float,
    reverse: bool,
) -> tuple[float, float]:
    if terminal_role == "TerminalLeadStart":
        along = length if reverse else 0.0
    else:
        along = 0.0 if reverse else length
    return (_coordinate(along), 0.0)


def _distance_squared(
    first: tuple[float, float], second: tuple[float, float]
) -> float:
    dx = first[0] - second[0]
    dy = first[1] - second[1]
    return (dx * dx) + (dy * dy)


def _presentation_point_dict(
    symbol: SchematicSymbolSpec,
    point: dict,
    length: float,
    reverse: bool,
    flip: bool,
) -> tuple[float, float]:
    return _presentation_point(
        symbol, (point["x"], point["y"]), length, reverse, flip
    )


def _lead_span_point_dict(
    symbol: SchematicSymbolSpec,
    point: dict,
    length: float,
    reverse: bool,
    flip: bool,
) -> tuple[float, float]:
    return _lead_span_point(symbol, (point["x"], point["y"]), length, reverse, flip)


def _presentation_point(
    symbol: SchematicSymbolSpec,
    point: tuple[float, float],
    length: float,
    reverse: bool,
    flip: bool,
) -> tuple[float, float]:
    along, across, base_length = _terminal_frame_point(symbol, point)
    scaled_along = along * length / base_length
    if reverse:
        scaled_along = length - scaled_along
    if flip:
        across = -across
    return (_coordinate(scaled_along), _coordinate(across))


def _lead_span_point(
    symbol: SchematicSymbolSpec,
    point: tuple[float, float],
    length: float,
    reverse: bool,
    flip: bool,
) -> tuple[float, float]:
    along, across, base_length = _terminal_frame_point(symbol, point)
    shifted_along = along + ((length - base_length) / 2)
    if reverse:
        shifted_along = length - shifted_along
    if flip:
        across = -across
    return (_coordinate(shifted_along), _coordinate(across))


def _terminal_frame_point(
    symbol: SchematicSymbolSpec,
    point: tuple[float, float],
) -> tuple[float, float, float]:
    pins = tuple(symbol.pins)
    start, base_length = _symbol_terminal_frame(symbol)
    end = pins[-1].at
    ux = (end[0] - start[0]) / base_length
    uy = (end[1] - start[1]) / base_length
    vx = -uy
    vy = ux
    dx = point[0] - start[0]
    dy = point[1] - start[1]
    return (dx * ux + dy * uy, dx * vx + dy * vy, base_length)


def _presentation_orientation(orientation: str, reverse: bool, flip: bool) -> str:
    result = _orientation(orientation)
    if reverse and result in ("Left", "Right"):
        result = "Left" if result == "Right" else "Right"
    if flip and result in ("Up", "Down"):
        result = "Up" if result == "Down" else "Down"
    return result


def _presentation_pin_matches(pins: tuple[dict, ...], ref: str | int) -> tuple[dict, ...]:
    if isinstance(ref, int):
        return tuple(pin for pin in pins if pin["number"] == str(ref))
    if not isinstance(ref, str):
        raise TypeError("Two-terminal anchors are addressed by name or pin number")
    by_number = tuple(pin for pin in pins if pin["number"] == ref)
    if by_number:
        return by_number
    return tuple(pin for pin in pins if pin["name"] == ref)


def _rotate_symbol_point(point: tuple[float, float], orientation: str) -> tuple[float, float]:
    x, y = point
    match _orientation(orientation):
        case "Right":
            return (x, y)
        case "Down":
            return (-y, x)
        case "Left":
            return (-x, -y)
        case "Up":
            return (y, -x)
    raise ValueError("Schematic orientation must be Right, Down, Left, or Up")


def _transform_symbol_point(
    point: tuple[float, float],
    origin: tuple[float, float],
    orientation: str,
) -> tuple[float, float]:
    rotated = _rotate_symbol_point(point, orientation)
    return (origin[0] + rotated[0], origin[1] + rotated[1])
