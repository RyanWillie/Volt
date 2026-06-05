"""Component library and schematic symbol authoring helpers."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Iterable, Iterator

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
from ._footprint import Footprint, FootprintInput, footprint_ref
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


class Part:
    """Reusable public part definition for Python-authored Volt libraries."""

    def __init__(
        self,
        *,
        name: str,
        pins: Iterable[PinSpec],
        symbol: SchematicSymbolSpec | Iterable[SchematicSymbolSpec] | None = None,
        schematic_symbol: SchematicSymbolSpec | Iterable[SchematicSymbolSpec] | None = None,
        footprint: FootprintInput | None = None,
        pads: dict[int | str, PinPadValue] | None = None,
        value: str | None = None,
        manufacturer: str | None = None,
        mpn: str | None = None,
        part_number: str | None = None,
        package: str | None = None,
        properties: dict | None = None,
        physical_properties: dict | None = None,
        ratings: dict | None = None,
        voltage_rating: float | None = None,
        power_rating: float | None = None,
        prefix: str = "U",
        extensions: dict | None = None,
        source_name: str | None = None,
        source_version: str | None = None,
    ) -> None:
        if not isinstance(name, str):
            raise TypeError("Part name must be a string")
        if not name:
            raise ValueError("Part name must not be empty")
        if not isinstance(prefix, str):
            raise TypeError("Part prefix must be a string")
        if not prefix:
            raise ValueError("Part prefix must not be empty")
        if symbol is not None and schematic_symbol is not None:
            raise TypeError("Part accepts either symbol or schematic_symbol")
        if mpn is not None and part_number is not None and mpn != part_number:
            raise ValueError("Part mpn and part_number must match when both are provided")

        logical_properties = dict(properties or {})
        if value is not None:
            logical_properties["value"] = value

        self.name = name
        self.pins = tuple(pins)
        for pin in self.pins:
            if not isinstance(pin, PinSpec):
                raise TypeError("Part pins must be PinSpec instances")
        self.schematic_symbols = _normalize_schematic_symbols(
            schematic_symbol if schematic_symbol is not None else symbol
        )
        self.footprint = footprint
        self.pads = None if pads is None else dict(pads)
        self.value = value
        self.manufacturer = manufacturer
        self.mpn = part_number if mpn is None else mpn
        self.package = package
        self.properties = logical_properties
        self.physical_properties = None if physical_properties is None else dict(physical_properties)
        self.ratings = dict(ratings or {})
        self.voltage_rating = voltage_rating
        self.power_rating = power_rating
        self.prefix = prefix
        self.extensions = dict(extensions or {})
        self.source_name = source_name or name
        self.source_version = source_version
        self._library: Library | None = None

    @property
    def library(self) -> Library | None:
        """Return the library this part was added to, if any."""
        return self._library

    @property
    def schematic_symbol(self) -> SchematicSymbolSpec | None:
        """Return this part's default schematic symbol, if one is registered."""
        return _schematic_symbol_for_variant(self.schematic_symbols, "default")

    @property
    def part_number(self) -> str | None:
        """Return the manufacturer part number carried by this part."""
        return self.mpn

    def _bind_library(self, library: Library) -> None:
        if self._library is not None and self._library is not library:
            raise ValueError(f"Part {self.name!r} already belongs to a different library")
        self._library = library

    def _to_library_component(self, library: Library | None = None) -> LibraryComponent:
        source = library or self._library or _StandaloneLibrarySource()
        return LibraryComponent(
            library=source,
            name=self.name,
            pins=self.pins,
            properties=self.properties,
            source_name=self.source_name,
            source_version=self.source_version or source.version,
            physical_part=self._physical_part_spec(),
            prefix=self.prefix,
            schematic_symbols=self.schematic_symbols,
        )

    def _physical_part_spec(self) -> PhysicalPartSpec | None:
        if self.footprint is None:
            return None
        return PhysicalPartSpec(
            manufacturer=self.manufacturer or "",
            part_number=self.mpn or "",
            package=self.package or _default_package(self.footprint),
            footprint=self.footprint,
            pin_pads=None if self.pads is None else dict(self.pads),
            properties=self.physical_properties,
            voltage_rating=self.voltage_rating,
            power_rating=self.power_rating,
        )

    def _to_dict(self) -> dict:
        payload = {
            "name": self.name,
            "pins": [pin._to_dict() for pin in self.pins],
            "schematic_symbols": [symbol._to_dict() for symbol in self.schematic_symbols],
            "footprint": _part_footprint_payload(self.footprint),
            "pads": _part_pads_payload(self.pads),
            "manufacturer": self.manufacturer,
            "mpn": self.mpn,
            "package": self.package,
            "properties": dict(self.properties),
            "physical_properties": (
                None if self.physical_properties is None else dict(self.physical_properties)
            ),
            "ratings": dict(self.ratings),
            "voltage_rating": self.voltage_rating,
            "power_rating": self.power_rating,
            "prefix": self.prefix,
            "extensions": dict(self.extensions),
            "source_name": self.source_name,
            "source_version": self.source_version,
        }
        if self.value is not None:
            payload["value"] = self.value
        return payload


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
        self._parts: dict[str, Part] = {}

    def add(self, part: Part) -> Part:
        """Add a reusable public part to this library."""
        if not isinstance(part, Part):
            raise TypeError("Library.add expects a Part")
        if part.name in self._parts or part.name in self._components:
            raise ValueError(f"Library part {part.name!r} already exists")
        part._bind_library(self)
        self._parts[part.name] = part
        return part

    def build(self) -> LibraryResult:
        """Validate this library's public parts and return a deterministic result."""
        return LibraryResult(self)

    @property
    def parts(self) -> tuple[Part, ...]:
        """Return public parts registered in this library in deterministic name order."""
        return tuple(self._parts[name] for name in sorted(self._parts))

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
        if name in self._components or name in self._parts:
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

    def __getitem__(self, name: str) -> Part | LibraryComponent:
        """Return a registered public part or legacy library component by name."""
        if name in self._parts:
            return self._parts[name]
        return self._components[name]


@dataclass(frozen=True)
class LibraryDiagnostic:
    """Diagnostic emitted while validating one library build."""

    source: str
    report: str
    severity: str
    code: str
    message: str
    entities: tuple[object, ...] = ()


class LibraryDiagnostics:
    """Ordered library diagnostics collected from part validation."""

    def __init__(self, diagnostics: Iterable[LibraryDiagnostic]):
        self._diagnostics = tuple(diagnostics)

    def __iter__(self) -> Iterator[LibraryDiagnostic]:
        """Iterate over diagnostics in deterministic report order."""
        return iter(self._diagnostics)

    def __len__(self) -> int:
        """Return the number of diagnostics."""
        return len(self._diagnostics)

    def __getitem__(self, index: int) -> LibraryDiagnostic:
        """Return one diagnostic by positional index."""
        return self._diagnostics[index]

    def errors(self, *, source: str | None = None) -> tuple[LibraryDiagnostic, ...]:
        """Return error diagnostics, optionally filtered by source."""
        return tuple(
            diagnostic
            for diagnostic in self._diagnostics
            if diagnostic.severity == "error"
            and (source is None or diagnostic.source == source)
        )

    @property
    def has_errors(self) -> bool:
        """Return whether any collected diagnostic has error severity."""
        return bool(self.errors())


@dataclass(frozen=True)
class LibraryPartResult:
    """Validation status for one public library part."""

    name: str
    schematic_ready: bool
    board_ready: bool
    serializable: bool
    has_footprint: bool
    pad_mapping_complete: bool
    diagnostics: tuple[LibraryDiagnostic, ...]


class LibraryResult:
    """Output of one deterministic library validation run."""

    def __init__(self, library: Library):
        self.library = library
        part_results: list[LibraryPartResult] = []
        diagnostics: list[LibraryDiagnostic] = []
        for part in library.parts:
            part_diagnostics = _validate_part(part)
            diagnostics.extend(part_diagnostics)
            part_results.append(_part_result(part, part_diagnostics))
        self._parts = tuple(part_results)
        self._diagnostics = LibraryDiagnostics(diagnostics)

    @property
    def ok(self) -> bool:
        """Return whether the library build has no error diagnostics."""
        return not self._diagnostics.has_errors

    @property
    def diagnostics(self) -> LibraryDiagnostics:
        """Return diagnostics collected during library validation."""
        return self._diagnostics

    @property
    def parts(self) -> tuple[LibraryPartResult, ...]:
        """Return per-part validation summaries in deterministic name order."""
        return self._parts

    def part(self, name: str) -> LibraryPartResult:
        """Return a per-part validation summary by part name."""
        for part in self._parts:
            if part.name == name:
                return part
        raise LookupError(f"Library result has no part named {name}")

    def to_dict(self) -> dict:
        """Return a deterministic dictionary payload for this library result."""
        return {
            "format": "volt.library_result",
            "schema_version": 1,
            "library": {
                "namespace": self.library.namespace,
                "version": self.library.version,
            },
            "ok": self.ok,
            "parts": [_part_result_payload(part) for part in self._parts],
            "diagnostics": _library_diagnostics_payload(self._diagnostics),
        }


@dataclass(frozen=True)
class _StandaloneLibrarySource:
    namespace: str = "volt.parts"
    version: str = "1.0.0"


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


def _validate_part(part: Part) -> tuple[LibraryDiagnostic, ...]:
    diagnostics: list[LibraryDiagnostic] = []
    source = f"part:{part.name}"

    if not part.pins:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_MISSING_PINS",
                f"Part {part.name} has no logical pins",
            )
        )

    if part.footprint is None:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_MISSING_FOOTPRINT",
                f"Part {part.name} has no selected footprint",
            )
        )
    elif not isinstance(part.footprint, Footprint):
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_MISSING_FOOTPRINT_GEOMETRY",
                f"Part {part.name} uses a footprint reference without reusable geometry",
            )
        )

    if part.pins and part.footprint is not None:
        diagnostics.extend(_validate_part_pad_mapping(part, source))

    try:
        json.dumps(part._to_dict(), sort_keys=True)
    except (TypeError, ValueError) as error:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_NON_SERIALIZABLE",
                f"Part {part.name} contains non-serializable data: {error}",
            )
        )

    return tuple(diagnostics)


def _validate_part_pad_mapping(part: Part, source: str) -> tuple[LibraryDiagnostic, ...]:
    diagnostics: list[LibraryDiagnostic] = []
    pads = part.pads or {}
    mapped_pin_numbers = _mapped_pin_numbers(part.pins, pads)
    missing_pin_numbers = [
        str(pin.number) for pin in part.pins if str(pin.number) not in mapped_pin_numbers
    ]
    if missing_pin_numbers:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_MISSING_PIN_MAPPING",
                "Part "
                f"{part.name} has logical pins without pad mappings: "
                + ", ".join(missing_pin_numbers),
            )
        )
        return tuple(diagnostics)

    if not isinstance(part.footprint, Footprint):
        return tuple(diagnostics)

    footprint_labels = {str(pad.label) for pad in part.footprint.pads}
    mapped_labels = _mapped_pad_labels(pads)
    unknown_labels = sorted(label for label in mapped_labels if label not in footprint_labels)
    if unknown_labels:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_UNKNOWN_PAD",
                f"Part {part.name} maps to unknown footprint pads: "
                + ", ".join(unknown_labels),
            )
        )
        return tuple(diagnostics)

    missing_electrical_labels = [
        str(pad.label)
        for pad in part.footprint.pads
        if _pad_requires_mapping(pad) and str(pad.label) not in mapped_labels
    ]
    if missing_electrical_labels:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_INCOMPLETE_PAD_MAPPING",
                f"Part {part.name} has electrical footprint pads without logical "
                "pin mappings: "
                + ", ".join(sorted(missing_electrical_labels)),
            )
        )

    return tuple(diagnostics)


def _part_result(part: Part, diagnostics: tuple[LibraryDiagnostic, ...]) -> LibraryPartResult:
    diagnostic_codes = {diagnostic.code for diagnostic in diagnostics}
    pad_mapping_complete = not (
        {
            "LIBRARY_PART_MISSING_PIN_MAPPING",
            "LIBRARY_PART_UNKNOWN_PAD",
            "LIBRARY_PART_INCOMPLETE_PAD_MAPPING",
        }
        & diagnostic_codes
    )
    has_footprint = isinstance(part.footprint, Footprint)
    return LibraryPartResult(
        name=part.name,
        schematic_ready=bool(part.pins and part.schematic_symbols),
        board_ready=bool(part.pins) and has_footprint and pad_mapping_complete,
        serializable="LIBRARY_PART_NON_SERIALIZABLE" not in diagnostic_codes,
        has_footprint=has_footprint,
        pad_mapping_complete=pad_mapping_complete,
        diagnostics=diagnostics,
    )


def _mapped_pin_numbers(
    pins: tuple[PinSpec, ...],
    pads: dict[int | str, PinPadValue],
) -> set[str]:
    pin_numbers = {str(pin.number) for pin in pins}
    names: dict[str, str | None] = {}
    for pin in pins:
        existing = names.get(pin.name)
        if existing is None and pin.name in names:
            continue
        if existing is not None:
            names[pin.name] = None
        else:
            names[pin.name] = str(pin.number)

    mapped: set[str] = set()
    for key in pads:
        key_text = str(key)
        if key_text in pin_numbers:
            mapped.add(key_text)
            continue
        named_number = names.get(key_text)
        if named_number is not None:
            mapped.add(named_number)
    return mapped


def _mapped_pad_labels(pads: dict[int | str, PinPadValue]) -> set[str]:
    labels: set[str] = set()
    for value in pads.values():
        for label in _pad_labels(value):
            labels.add(label)
    return labels


def _pad_labels(value: PinPadValue) -> tuple[str, ...]:
    if isinstance(value, (tuple, list)):
        return tuple(str(item) for item in value)
    return (str(value),)


def _pad_requires_mapping(pad) -> bool:
    return getattr(pad, "mechanical_role", None) is None


def _library_diagnostic(source: str, code: str, message: str) -> LibraryDiagnostic:
    return LibraryDiagnostic(
        source=source,
        report="library.part",
        severity="error",
        code=code,
        message=message,
    )


def _part_result_payload(part: LibraryPartResult) -> dict:
    return {
        "name": part.name,
        "schematic_ready": part.schematic_ready,
        "board_ready": part.board_ready,
        "serializable": part.serializable,
        "has_footprint": part.has_footprint,
        "pad_mapping_complete": part.pad_mapping_complete,
        "diagnostics": [
            {
                "source": diagnostic.source,
                "report": diagnostic.report,
                "severity": diagnostic.severity,
                "code": diagnostic.code,
                "message": diagnostic.message,
                "entities": list(diagnostic.entities),
            }
            for diagnostic in part.diagnostics
        ],
    }


def _library_diagnostics_payload(diagnostics: LibraryDiagnostics) -> dict:
    return {
        "summary": _library_diagnostic_summary(diagnostics),
        "diagnostics": [
            {
                "source": diagnostic.source,
                "report": diagnostic.report,
                "severity": diagnostic.severity,
                "code": diagnostic.code,
                "message": diagnostic.message,
                "entities": list(diagnostic.entities),
            }
            for diagnostic in diagnostics
        ],
    }


def _library_diagnostic_summary(diagnostics: LibraryDiagnostics) -> dict[str, int]:
    summary = {"errors": 0, "infos": 0, "warnings": 0}
    for diagnostic in diagnostics:
        if diagnostic.severity == "error":
            summary["errors"] += 1
        elif diagnostic.severity == "warning":
            summary["warnings"] += 1
        else:
            summary["infos"] += 1
    return summary


def _part_footprint_payload(footprint: FootprintInput | None) -> dict | None:
    if footprint is None:
        return None
    library, name = footprint_ref(footprint)
    result = {"library": library, "name": name}
    if isinstance(footprint, Footprint):
        result["pads"] = [pad._to_dict() for pad in footprint.pads]
    return result


def _part_pads_payload(pads: dict[int | str, PinPadValue] | None) -> list[dict[str, object]]:
    if pads is None:
        return []
    return [
        {"pin": str(key), "pads": list(_pad_labels(value))}
        for key, value in sorted(pads.items(), key=lambda item: str(item[0]))
    ]


def _default_package(footprint: FootprintInput) -> str:
    _library, name = footprint_ref(footprint)
    return name
