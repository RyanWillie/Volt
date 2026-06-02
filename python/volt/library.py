"""Component library and schematic symbol authoring helpers."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

from ._library_symbol_builders import (
    _default_two_terminal_symbol_spec,
    _orientation,
    _schematic_block_pin_side,
    _schematic_block_symbol_spec,
    _symbol_line_role,
    _symbol_point,
    _terminal_lead_line_role,
    _text_horizontal_alignment,
    _text_vertical_alignment,
)
from ._footprint import FootprintInput
from ._utils import _coordinate, _number, _positive_coordinate

PinPadValue = str | tuple[str, ...] | list[str]


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
    """Reusable selected physical part data; board-ready real parts should carry Footprint objects."""

    manufacturer: str
    part_number: str
    package: str
    footprint: FootprintInput
    pin_pads: dict[int | str, PinPadValue] | None = None
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
        footprint: FootprintInput,
        properties: dict | None = None,
        voltage_rating: float | None = None,
        power_rating: float | None = None,
    ) -> PhysicalPartSpec:
        """Create a physical part whose footprint pad labels match pin numbers."""
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

    def pin_pads_for(self, component: LibraryComponent) -> dict[int | str, PinPadValue]:
        """Return the pin-to-pad mapping for a library component."""
        if self.same_numbered_pads:
            result: dict[int | str, PinPadValue] = {}
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
class SchematicBlockSideLayoutSpec:
    """Per-side placement overrides for generic block and IC schematic symbols."""

    side: str
    pad: float | None = None
    lead_length: float | None = None
    pin_pitch: float | None = None
    pin_label_offset: float | None = None
    pin_number_offset: float | None = None

    def __post_init__(self) -> None:
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
        """Create a pin anchor for a custom schematic symbol."""
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
        """Create a pin placement entry for block-style schematic symbols."""
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
        """Create a pin placement entry for IC-style schematic symbols."""
        return SchematicSymbolSpec.block_pin(
            name,
            number,
            side=side,
            slot=slot,
            label=label,
        )

    @staticmethod
    def side_layout(
        side: str,
        *,
        pad: float | None = None,
        lead_length: float | None = None,
        pin_pitch: float | None = None,
        pin_label_offset: float | None = None,
        pin_number_offset: float | None = None,
    ) -> SchematicBlockSideLayoutSpec:
        """Create per-side layout overrides for a block-style symbol."""
        return SchematicBlockSideLayoutSpec(
            side,
            pad=pad,
            lead_length=lead_length,
            pin_pitch=pin_pitch,
            pin_label_offset=pin_label_offset,
            pin_number_offset=pin_number_offset,
        )

    @staticmethod
    def block(
        name: str,
        *,
        pins: Iterable[SchematicBlockPinSpec],
        width: float | None = None,
        height: float | None = None,
        lead_length: float = 6,
        pin_pitch: float = 10,
        pin_label_offset: float = 2,
        side_layouts: Iterable[SchematicBlockSideLayoutSpec] = (),
        center_label: str | None = None,
        bottom_label: str | None = None,
        pin_labels: bool = True,
        pin_numbers: bool = False,
        pin_number_offset: float = 2,
        variant: str = "default",
    ) -> SchematicSymbolSpec:
        """Build a rectangular block schematic symbol from pin placement specs."""
        return _schematic_block_symbol_spec(
            name,
            pins=pins,
            width=width,
            height=height,
            lead_length=lead_length,
            pin_pitch=pin_pitch,
            pin_label_offset=pin_label_offset,
            side_layouts=side_layouts,
            center_label=center_label,
            bottom_label=bottom_label,
            pin_labels=pin_labels,
            pin_numbers=pin_numbers,
            pin_number_offset=pin_number_offset,
            variant=variant,
        )

    @staticmethod
    def ic(
        name: str,
        *,
        pins: Iterable[SchematicBlockPinSpec],
        width: float | None = None,
        height: float | None = None,
        lead_length: float = 6,
        pin_pitch: float = 10,
        pin_label_offset: float = 2,
        side_layouts: Iterable[SchematicBlockSideLayoutSpec] = (),
        center_label: str | None = None,
        bottom_label: str | None = None,
        pin_labels: bool = True,
        pin_numbers: bool = False,
        pin_number_offset: float = 2,
        variant: str = "default",
    ) -> SchematicSymbolSpec:
        """Build an IC-style schematic symbol from pin placement specs."""
        return SchematicSymbolSpec.block(
            name,
            pins=pins,
            width=width,
            height=height,
            lead_length=lead_length,
            pin_pitch=pin_pitch,
            pin_label_offset=pin_label_offset,
            side_layouts=side_layouts,
            center_label=center_label,
            bottom_label=bottom_label,
            pin_labels=pin_labels,
            pin_numbers=pin_numbers,
            pin_number_offset=pin_number_offset,
            variant=variant,
        )

    @staticmethod
    def line(
        start: tuple[float, float],
        end: tuple[float, float],
        *,
        role: str | None = None,
    ) -> dict:
        """Create a line primitive for a custom schematic symbol."""
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
        """Create a terminal lead line primitive for a custom schematic symbol."""
        return SchematicSymbolSpec.line(
            start,
            end,
            role=_terminal_lead_line_role(terminal),
        )

    @staticmethod
    def rectangle(first_corner: tuple[float, float], second_corner: tuple[float, float]) -> dict:
        """Create a rectangle primitive for a custom schematic symbol."""
        return {
            "type": "rectangle",
            "first_corner": _symbol_point(first_corner),
            "second_corner": _symbol_point(second_corner),
        }

    @staticmethod
    def circle(center: tuple[float, float], radius: float) -> dict:
        """Create a circle primitive for a custom schematic symbol."""
        return {"type": "circle", "center": _symbol_point(center), "radius": _coordinate(radius)}

    @staticmethod
    def arc(
        center: tuple[float, float],
        radius: float,
        start_degrees: float,
        sweep_degrees: float,
    ) -> dict:
        """Create an arc primitive for a custom schematic symbol."""
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
        *,
        align: str = "middle",
        baseline: str = "baseline",
        font_size: float | None = None,
    ) -> dict:
        """Create a text primitive for a custom schematic symbol."""
        primitive = {
            "type": "text",
            "text": text,
            "anchor": _symbol_point(at),
            "orientation": _orientation(orientation),
        }
        horizontal = _text_horizontal_alignment(align)
        vertical = _text_vertical_alignment(baseline)
        if horizontal != "Middle":
            primitive["horizontal_alignment"] = horizontal
        if vertical != "Baseline":
            primitive["vertical_alignment"] = vertical
        if font_size is not None:
            primitive["font_size"] = _positive_coordinate(
                font_size,
                "Schematic text font sizes",
            )
        return primitive


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
        """Return the stable design-local cache key for this library component."""
        return (self.library.namespace, self.source_name, self.source_version, self.name)

    @property
    def schematic_symbol(self) -> SchematicSymbolSpec | None:
        """Return this component's default schematic symbol, if one is registered."""
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
        """Register a reusable component entry in this Python library."""
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
        """Return a registered library component by name."""
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
