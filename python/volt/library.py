"""Component library and schematic symbol authoring helpers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

from ._utils import _coordinate, _nonnegative_coordinate, _number, _orientation, _positive_coordinate


@dataclass(frozen=True)
class PinSpec:
    """Reusable pin definition data for Python-authored component definitions."""

    name: str
    number: int | str
    role: str = "passive"
    requirement: str = "required"
    terminal: str = "unspecified"
    direction: str = "unspecified"
    signal: str = "unspecified"
    drive: str = "unspecified"
    polarity: str = "none"
    voltage_range: tuple[float | None, float | None] | None = None

    def _to_dict(self):
        result = {
            "name": self.name,
            "number": str(self.number),
            "role": self.role,
            "requirement": self.requirement,
            "terminal": self.terminal,
            "direction": self.direction,
            "signal": self.signal,
            "drive": self.drive,
            "polarity": self.polarity,
        }
        if self.voltage_range is not None:
            if not isinstance(self.voltage_range, tuple) or len(self.voltage_range) != 2:
                raise TypeError("voltage_range must be a (minimum, maximum) tuple")
            minimum, maximum = self.voltage_range
            result["voltage_range"] = (
                None if minimum is None else _number(minimum),
                None if maximum is None else _number(maximum),
            )
        return result


@dataclass(frozen=True)
class PhysicalPartSpec:
    """Reusable selected physical part data for library-authored components."""

    manufacturer: str
    part_number: str
    package: str
    footprint: tuple[str, str]
    pin_pads: dict[int | str, str] | None = None
    properties: dict | None = None
    voltage_rating: float | None = None
    power_rating: float | None = None
    same_numbered_pads: bool = False

    @classmethod
    def same_numbered(
        cls,
        *,
        manufacturer: str,
        part_number: str,
        package: str,
        footprint: tuple[str, str],
        properties: dict | None = None,
        voltage_rating: float | None = None,
        power_rating: float | None = None,
    ) -> PhysicalPartSpec:
        return cls(
            manufacturer=manufacturer,
            part_number=part_number,
            package=package,
            footprint=footprint,
            properties=properties,
            voltage_rating=voltage_rating,
            power_rating=power_rating,
            same_numbered_pads=True,
        )

    def pin_pads_for(self, component: LibraryComponent) -> dict[int | str, str]:
        if self.same_numbered_pads:
            result: dict[int | str, str] = {}
            for pin in component.pins:
                number = pin.number
                key: int | str
                if isinstance(number, str) and number.isdigit():
                    key = int(number)
                else:
                    key = number
                result[key] = str(number)
            return result
        if self.pin_pads is None:
            raise ValueError("physical part requires pin_pads or same_numbered_pads")
        return dict(self.pin_pads)


@dataclass(frozen=True)
class SchematicSymbolPinSpec:
    """Reusable visual pin anchor data for Python-authored schematic symbols."""

    name: str
    number: int | str
    at: tuple[float, float]
    orientation: str = "Right"

    def _to_dict(self):
        return {
            "name": self.name,
            "number": str(self.number),
            "anchor": _symbol_point(self.at),
            "orientation": _orientation(self.orientation),
        }


@dataclass(frozen=True)
class SchematicBlockPinSpec:
    """Pin placement input for generic block and IC schematic symbols."""

    name: str
    number: int | str
    side: str
    slot: int | None = None
    label: str | None = None

    def __post_init__(self) -> None:
        if not isinstance(self.name, str):
            raise TypeError("Schematic block pin names must be strings")
        if not self.name:
            raise ValueError("Schematic block pin names must not be empty")
        number = str(self.number)
        if not number:
            raise ValueError("Schematic block pin numbers must not be empty")
        if self.slot is not None:
            if isinstance(self.slot, bool) or not isinstance(self.slot, int):
                raise TypeError("Schematic block pin slots must be integers")
            if self.slot <= 0:
                raise ValueError("Schematic block pin slots must be positive")
        if self.label is not None:
            if not isinstance(self.label, str):
                raise TypeError("Schematic block pin labels must be strings")
            if not self.label:
                raise ValueError("Schematic block pin labels must not be empty")
        object.__setattr__(self, "number", number)
        object.__setattr__(self, "side", _schematic_block_pin_side(self.side))


@dataclass(frozen=True)
class SchematicSymbolSpec:
    """Reusable Volt-native schematic symbol data for Python-authored libraries."""

    name: str
    pins: tuple[SchematicSymbolPinSpec, ...]
    primitives: tuple[dict, ...]
    variant: str = "default"

    def __post_init__(self) -> None:
        if not self.variant:
            raise ValueError("Schematic symbol variant must not be empty")
        object.__setattr__(self, "pins", tuple(self.pins))
        object.__setattr__(
            self,
            "primitives",
            tuple(dict(primitive) for primitive in self.primitives),
        )

    def _to_dict(self):
        return {
            "name": self.name,
            "pins": [pin._to_dict() for pin in self.pins],
            "primitives": [dict(primitive) for primitive in self.primitives],
        }

    @staticmethod
    def pin(
        name: str,
        number: int | str,
        at: tuple[float, float],
        orientation: str = "Right",
    ) -> SchematicSymbolPinSpec:
        return SchematicSymbolPinSpec(name, number, at, orientation)

    @staticmethod
    def block_pin(
        name: str,
        number: int | str,
        *,
        side: str,
        slot: int | None = None,
        label: str | None = None,
    ) -> SchematicBlockPinSpec:
        return SchematicBlockPinSpec(name, number, side=side, slot=slot, label=label)

    @staticmethod
    def ic_pin(
        name: str,
        number: int | str,
        *,
        side: str,
        slot: int | None = None,
        label: str | None = None,
    ) -> SchematicBlockPinSpec:
        return SchematicSymbolSpec.block_pin(
            name,
            number,
            side=side,
            slot=slot,
            label=label,
        )

    @staticmethod
    def block(
        name: str,
        *,
        pins: Iterable[SchematicBlockPinSpec],
        width: float | None = None,
        height: float | None = None,
        lead_length: float = 10,
        pin_pitch: float = 10,
        pin_label_offset: float = 3,
        center_label: str | None = None,
        bottom_label: str | None = None,
        pin_labels: bool = True,
        variant: str = "default",
    ) -> SchematicSymbolSpec:
        return _schematic_block_symbol_spec(
            name,
            pins=pins,
            width=width,
            height=height,
            lead_length=lead_length,
            pin_pitch=pin_pitch,
            pin_label_offset=pin_label_offset,
            center_label=center_label,
            bottom_label=bottom_label,
            pin_labels=pin_labels,
            variant=variant,
        )

    @staticmethod
    def ic(
        name: str,
        *,
        pins: Iterable[SchematicBlockPinSpec],
        width: float | None = None,
        height: float | None = None,
        lead_length: float = 10,
        pin_pitch: float = 10,
        pin_label_offset: float = 3,
        center_label: str | None = None,
        bottom_label: str | None = None,
        pin_labels: bool = True,
        variant: str = "default",
    ) -> SchematicSymbolSpec:
        return SchematicSymbolSpec.block(
            name,
            pins=pins,
            width=width,
            height=height,
            lead_length=lead_length,
            pin_pitch=pin_pitch,
            pin_label_offset=pin_label_offset,
            center_label=center_label,
            bottom_label=bottom_label,
            pin_labels=pin_labels,
            variant=variant,
        )

    @staticmethod
    def line(
        start: tuple[float, float],
        end: tuple[float, float],
        *,
        role: str | None = None,
    ) -> dict:
        primitive = {"type": "line", "start": _symbol_point(start), "end": _symbol_point(end)}
        if role is not None:
            primitive["role"] = _symbol_line_role(role)
        return primitive

    @staticmethod
    def terminal_lead(
        start: tuple[float, float],
        end: tuple[float, float],
        *,
        terminal: str,
    ) -> dict:
        return SchematicSymbolSpec.line(
            start,
            end,
            role=_terminal_lead_line_role(terminal),
        )

    @staticmethod
    def rectangle(first_corner: tuple[float, float], second_corner: tuple[float, float]) -> dict:
        return {
            "type": "rectangle",
            "first_corner": _symbol_point(first_corner),
            "second_corner": _symbol_point(second_corner),
        }

    @staticmethod
    def circle(center: tuple[float, float], radius: float) -> dict:
        return {"type": "circle", "center": _symbol_point(center), "radius": _coordinate(radius)}

    @staticmethod
    def arc(
        center: tuple[float, float],
        radius: float,
        start_degrees: float,
        sweep_degrees: float,
    ) -> dict:
        return {
            "type": "arc",
            "center": _symbol_point(center),
            "radius": _coordinate(radius),
            "start_degrees": _coordinate(start_degrees),
            "sweep_degrees": _coordinate(sweep_degrees),
        }

    @staticmethod
    def text(
        text: str,
        at: tuple[float, float],
        orientation: str = "Right",
    ) -> dict:
        return {
            "type": "text",
            "text": text,
            "anchor": _symbol_point(at),
            "orientation": _orientation(orientation),
        }


@dataclass(frozen=True)
class LibraryComponent:
    """Reusable component entry owned by a Python library."""

    library: Library
    name: str
    pins: tuple[PinSpec, ...]
    properties: dict
    source_name: str
    source_version: str
    physical_part: PhysicalPartSpec | None = None
    prefix: str = "U"
    schematic_symbols: tuple[SchematicSymbolSpec, ...] = ()

    @property
    def cache_key(self) -> tuple[str, str, str, str]:
        return (self.library.namespace, self.source_name, self.source_version, self.name)

    @property
    def schematic_symbol(self) -> SchematicSymbolSpec | None:
        return _schematic_symbol_for_variant(self.schematic_symbols, "default")


class Library:
    """Collection of reusable Python-authored component definitions."""

    def __init__(self, namespace: str, *, version: str = "1.0.0"):
        if not namespace:
            raise ValueError("Library namespace must not be empty")
        if not version:
            raise ValueError("Library version must not be empty")
        self.namespace = namespace
        self.version = version
        self._components: dict[str, LibraryComponent] = {}

    def component(
        self,
        name: str,
        *,
        pins: Iterable[PinSpec],
        properties: dict | None = None,
        source_name: str | None = None,
        source_version: str | None = None,
        physical_part: PhysicalPartSpec | None = None,
        prefix: str = "U",
        schematic_symbol: SchematicSymbolSpec | Iterable[SchematicSymbolSpec] | None = None,
    ) -> LibraryComponent:
        if not name:
            raise ValueError("Library component name must not be empty")
        if not prefix:
            raise ValueError("Library component prefix must not be empty")
        if name in self._components:
            raise ValueError(f"Library component {name!r} already exists")
        component = LibraryComponent(
            library=self,
            name=name,
            pins=tuple(pins),
            properties=dict(properties or {}),
            source_name=source_name or name,
            source_version=source_version or self.version,
            physical_part=physical_part,
            prefix=prefix,
            schematic_symbols=_normalize_schematic_symbols(schematic_symbol),
        )
        self._components[name] = component
        return component

    def __getitem__(self, name: str) -> LibraryComponent:
        return self._components[name]


def _normalize_schematic_symbols(
    symbols: SchematicSymbolSpec | Iterable[SchematicSymbolSpec] | None,
) -> tuple[SchematicSymbolSpec, ...]:
    if symbols is None:
        return ()
    if isinstance(symbols, SchematicSymbolSpec):
        result = (symbols,)
    else:
        result = tuple(symbols)
    variants = set()
    for symbol in result:
        if not isinstance(symbol, SchematicSymbolSpec):
            raise TypeError("schematic_symbol entries must be SchematicSymbolSpec instances")
        if symbol.variant in variants:
            raise ValueError("schematic symbol variants must be unique")
        variants.add(symbol.variant)
    return result


def _schematic_symbol_refs(symbols: Iterable[SchematicSymbolSpec]) -> list[dict[str, str]]:
    return [{"name": symbol.name, "variant": symbol.variant} for symbol in symbols]


def _schematic_symbol_for_variant(
    symbols: Iterable[SchematicSymbolSpec], variant: str
) -> SchematicSymbolSpec | None:
    for symbol in symbols:
        if symbol.variant == variant:
            return symbol
    return None



def _symbol_point(value: tuple[float, float]) -> dict:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise TypeError("Schematic symbol points must be (x, y) pairs")
    return {"x": _coordinate(value[0]), "y": _coordinate(value[1])}


def _symbol_line_role(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic symbol line roles must be strings")
    normalized = {
        "normal": "Normal",
        "terminalleadstart": "TerminalLeadStart",
        "terminal_lead_start": "TerminalLeadStart",
        "terminal-lead-start": "TerminalLeadStart",
        "start": "TerminalLeadStart",
        "terminalleadend": "TerminalLeadEnd",
        "terminal_lead_end": "TerminalLeadEnd",
        "terminal-lead-end": "TerminalLeadEnd",
        "end": "TerminalLeadEnd",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError(
            "Schematic symbol line roles must be Normal, TerminalLeadStart, or TerminalLeadEnd"
        )
    return normalized


def _terminal_lead_line_role(terminal: str) -> str:
    if not isinstance(terminal, str):
        raise TypeError("Schematic terminal lead names must be strings")
    normalized = {
        "start": "TerminalLeadStart",
        "1": "TerminalLeadStart",
        "left": "TerminalLeadStart",
        "end": "TerminalLeadEnd",
        "2": "TerminalLeadEnd",
        "right": "TerminalLeadEnd",
    }.get(terminal.casefold())
    if normalized is None:
        raise ValueError("Schematic terminal leads must target start or end")
    return normalized


def _schematic_block_pin_side(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic block pin sides must be strings")
    normalized = {
        "l": "Left",
        "left": "Left",
        "r": "Right",
        "right": "Right",
        "t": "Up",
        "top": "Up",
        "up": "Up",
        "u": "Up",
        "b": "Down",
        "bottom": "Down",
        "down": "Down",
        "d": "Down",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError("Schematic block pin side must be left, right, top, or bottom")
    return normalized


def _orientation(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic orientation must be a string")
    normalized = {
        "right": "Right",
        "down": "Down",
        "left": "Left",
        "up": "Up",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError("Schematic orientation must be Right, Down, Left, or Up")
    return normalized


def _schematic_block_symbol_spec(
    name: str,
    *,
    pins: Iterable[SchematicBlockPinSpec],
    width: float | None,
    height: float | None,
    lead_length: float,
    pin_pitch: float,
    pin_label_offset: float,
    center_label: str | None,
    bottom_label: str | None,
    pin_labels: bool,
    variant: str,
) -> SchematicSymbolSpec:
    if not isinstance(name, str):
        raise TypeError("Schematic block symbol names must be strings")
    if not name:
        raise ValueError("Schematic block symbol names must not be empty")
    block_pins = tuple(_schematic_block_pin_entry(pin) for pin in pins)
    if not block_pins:
        raise ValueError("Schematic block symbols need at least one pin")
    if not isinstance(pin_labels, bool):
        raise TypeError("Schematic block symbol pin_labels must be a boolean")

    pitch = _positive_coordinate(pin_pitch, "Schematic block symbol pin pitches")
    lead = _nonnegative_coordinate(lead_length, "Schematic block symbol lead lengths")
    label_offset = _nonnegative_coordinate(
        pin_label_offset,
        "Schematic block symbol pin label offsets",
    )
    slots = _schematic_block_pin_slots(block_pins)
    horizontal_max = max(
        (slot for pin, slot in slots if pin.side in ("Up", "Down")),
        default=0,
    )
    vertical_max = max(
        (slot for pin, slot in slots if pin.side in ("Left", "Right")),
        default=0,
    )
    body_width = (
        _positive_coordinate(width, "Schematic block symbol widths")
        if width is not None
        else max(horizontal_max + 1, 4) * pitch
    )
    body_height = (
        _positive_coordinate(height, "Schematic block symbol heights")
        if height is not None
        else max(vertical_max + 1, 4) * pitch
    )
    if horizontal_max * pitch > body_width:
        raise ValueError("Schematic block symbol width is too small for top or bottom pin slots")
    if vertical_max * pitch > body_height:
        raise ValueError("Schematic block symbol height is too small for left or right pin slots")

    center_label = _optional_symbol_text(center_label, "center label")
    bottom_label = _optional_symbol_text(bottom_label, "bottom label")

    body_left = lead
    body_top = 0.0
    body_right = lead + body_width
    body_bottom = body_height
    symbol_pins = []
    primitives = [
        SchematicSymbolSpec.rectangle((body_left, body_top), (body_right, body_bottom))
    ]
    if center_label is not None:
        primitives.append(
            SchematicSymbolSpec.text(
                center_label,
                (body_left + body_width / 2, body_top + body_height / 2),
            )
        )
    if bottom_label is not None:
        primitives.append(
            SchematicSymbolSpec.text(
                bottom_label,
                (body_left + body_width / 2, body_bottom + lead + label_offset),
            )
        )

    seen_numbers: set[str] = set()
    for pin, slot in slots:
        if pin.number in seen_numbers:
            raise ValueError(f"Schematic block symbol pin number {pin.number!r} is duplicated")
        seen_numbers.add(pin.number)
        anchor, body = _schematic_block_pin_points(
            pin.side,
            slot=slot,
            pitch=pitch,
            body_left=body_left,
            body_right=body_right,
            body_top=body_top,
            body_bottom=body_bottom,
            lead=lead,
        )
        symbol_pins.append(SchematicSymbolSpec.pin(pin.name, pin.number, anchor, pin.side))
        primitives.append(SchematicSymbolSpec.line(anchor, body))
        if pin_labels:
            primitives.append(
                SchematicSymbolSpec.text(
                    pin.label or pin.name,
                    _schematic_block_pin_label_point(
                        pin.side,
                        body=body,
                        offset=label_offset,
                    ),
                )
            )

    return SchematicSymbolSpec(
        name,
        pins=tuple(symbol_pins),
        primitives=tuple(primitives),
        variant=variant,
    )


def _schematic_block_pin_entry(value) -> SchematicBlockPinSpec:
    if not isinstance(value, SchematicBlockPinSpec):
        raise TypeError("Schematic block symbol pins must be SchematicBlockPinSpec entries")
    return value


def _schematic_block_pin_slots(
    pins: tuple[SchematicBlockPinSpec, ...],
) -> tuple[tuple[SchematicBlockPinSpec, int], ...]:
    used: dict[str, set[int]] = {"Left": set(), "Right": set(), "Up": set(), "Down": set()}
    next_slot = {side: 1 for side in used}
    result = []
    for pin in pins:
        slot = pin.slot
        if slot is None:
            while next_slot[pin.side] in used[pin.side]:
                next_slot[pin.side] += 1
            slot = next_slot[pin.side]
        if slot in used[pin.side]:
            raise ValueError(
                f"Schematic block symbol {pin.side.lower()} pin slot {slot} is duplicated"
            )
        used[pin.side].add(slot)
        result.append((pin, slot))
    return tuple(result)


def _schematic_block_pin_points(
    side: str,
    *,
    slot: int,
    pitch: float,
    body_left: float,
    body_right: float,
    body_top: float,
    body_bottom: float,
    lead: float,
) -> tuple[tuple[float, float], tuple[float, float]]:
    offset = slot * pitch
    if side == "Left":
        return (body_left - lead, body_top + offset), (body_left, body_top + offset)
    if side == "Right":
        return (body_right + lead, body_top + offset), (body_right, body_top + offset)
    if side == "Up":
        return (body_left + offset, body_top - lead), (body_left + offset, body_top)
    return (body_left + offset, body_bottom + lead), (body_left + offset, body_bottom)


def _schematic_block_pin_label_point(
    side: str,
    *,
    body: tuple[float, float],
    offset: float,
) -> tuple[float, float]:
    x, y = body
    if side == "Left":
        return (x + offset, y)
    if side == "Right":
        return (x - offset, y)
    if side == "Up":
        return (x, y + offset)
    return (x, y - offset)


def _optional_symbol_text(value: str | None, label: str) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str):
        raise TypeError(f"Schematic block symbol {label} must be a string")
    if not value:
        raise ValueError(f"Schematic block symbol {label} must not be empty")
    return value


def _default_two_terminal_symbol_spec(name: str) -> SchematicSymbolSpec | None:
    # Production defaults keep identity in fields or explicitly authored text, not
    # internal debug glyphs embedded in generic element geometry.
    if name in ("resistor", "volt.passives:resistor"):
        return _resistor_symbol_spec(name)
    if name in ("capacitor", "volt.passives:capacitor"):
        return _capacitor_symbol_spec(name)
    if name in ("inductor", "volt.passives:inductor"):
        return _inductor_symbol_spec(name)
    if name in ("diode", "volt.discretes:diode"):
        return _diode_symbol_spec(name)
    if name in ("led", "volt.optos:led"):
        return _led_symbol_spec(name)
    return None


def _two_terminal_pins(
    left_name: str,
    left_number: int | str,
    right_name: str,
    right_number: int | str,
) -> tuple[SchematicSymbolPinSpec, SchematicSymbolPinSpec]:
    return (
        SchematicSymbolSpec.pin(left_name, left_number, (0, 0), "Left"),
        SchematicSymbolSpec.pin(right_name, right_number, (20, 0), "Right"),
    )


def _resistor_symbol_spec(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("1", 1, "2", 2),
        primitives=(
            SchematicSymbolSpec.terminal_lead((0, 0), (4, 0), terminal="start"),
            SchematicSymbolSpec.rectangle((4, -3), (16, 3)),
            SchematicSymbolSpec.terminal_lead((16, 0), (20, 0), terminal="end"),
        ),
    )


def _capacitor_symbol_spec(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("1", 1, "2", 2),
        primitives=(
            SchematicSymbolSpec.terminal_lead((0, 0), (8, 0), terminal="start"),
            SchematicSymbolSpec.line((8, -5), (8, 5)),
            SchematicSymbolSpec.line((12, -5), (12, 5)),
            SchematicSymbolSpec.terminal_lead((12, 0), (20, 0), terminal="end"),
        ),
    )


def _inductor_symbol_spec(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("1", 1, "2", 2),
        primitives=(
            SchematicSymbolSpec.terminal_lead((0, 0), (4, 0), terminal="start"),
            SchematicSymbolSpec.arc((6, 0), 2, 180, -180),
            SchematicSymbolSpec.arc((10, 0), 2, 180, -180),
            SchematicSymbolSpec.arc((14, 0), 2, 180, -180),
            SchematicSymbolSpec.terminal_lead((16, 0), (20, 0), terminal="end"),
        ),
    )


def _diode_symbol_spec(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("K", 1, "A", 2),
        primitives=(
            SchematicSymbolSpec.terminal_lead((0, 0), (7, 0), terminal="start"),
            SchematicSymbolSpec.line((7, -5), (7, 5)),
            SchematicSymbolSpec.line((7, -5), (13, 0)),
            SchematicSymbolSpec.line((7, 5), (13, 0)),
            SchematicSymbolSpec.terminal_lead((13, 0), (20, 0), terminal="end"),
        ),
    )


def _led_symbol_spec(name: str) -> SchematicSymbolSpec:
    diode = _diode_symbol_spec(name)
    return SchematicSymbolSpec(
        name,
        pins=diode.pins,
        primitives=(
            *diode.primitives,
            SchematicSymbolSpec.line((13, -6), (17, -10)),
            SchematicSymbolSpec.line((15, -4), (19, -8)),
        ),
    )
