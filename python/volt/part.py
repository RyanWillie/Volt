"""Public reusable part definitions for Volt Python libraries."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
from typing import TYPE_CHECKING, Iterable

from ._footprint import Footprint, FootprintInput, footprint_ref
from ._immutable import _freeze_value, _mutable_value
from .library import (
    PartModel3D,
    PhysicalPartSpec,
    PinPadValue,
    PinSpec,
    SchematicSymbolSpec,
    _normalize_schematic_symbols,
    _schematic_symbol_for_variant,
)

if TYPE_CHECKING:
    from .library import Library


@dataclass(frozen=True)
class _PartDefinition:
    """Normalized part lowering data shared by new parts and compatibility specs."""

    name: str
    pins: tuple[PinSpec, ...]
    properties: dict
    source_namespace: str
    source_name: str
    source_version: str
    physical_part: PhysicalPartSpec | None = None
    prefix: str = "U"
    schematic_symbols: tuple[SchematicSymbolSpec, ...] = ()

    @property
    def cache_key(self) -> tuple[str, str, str, str]:
        """Return the stable design-local cache key for this part definition."""
        return (
            self.source_namespace,
            self.source_name,
            self.source_version,
            self.name,
        )

    @property
    def schematic_symbol(self) -> SchematicSymbolSpec | None:
        """Return this definition's default schematic symbol, if one is registered."""
        return _schematic_symbol_for_variant(self.schematic_symbols, "default")


class Part:
    """Reusable public part definition for Python-authored Volt libraries."""

    def __setattr__(self, name: str, value: object) -> None:
        if getattr(self, "_frozen", False):
            raise AttributeError("Part definitions are immutable")
        object.__setattr__(self, name, value)

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
        model_3d: PartModel3D | None = None,
        approved_alternate_mpns: Iterable[str] = (),
        orderable: PhysicalPartSpec | None = None,
        orderable_part: PhysicalPartSpec | None = None,
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
        if orderable is not None:
            if orderable_part is not None:
                raise TypeError("Part accepts either orderable or orderable_part")
            orderable_part = orderable
        normalized_orderable_part = orderable_part
        alternate_mpns = tuple(str(mpn) for mpn in approved_alternate_mpns)
        if orderable_part is not None:
            if (
                footprint is not None
                or pads is not None
                or manufacturer is not None
                or mpn is not None
                or part_number is not None
                or package is not None
                or physical_properties is not None
                or voltage_rating is not None
                or power_rating is not None
                or model_3d is not None
                or alternate_mpns
            ):
                raise TypeError(
                    "Part accepts either orderable_part or explicit physical part fields"
                )
            footprint = orderable_part.footprint
            pads = orderable_part.pin_pads
            manufacturer = orderable_part.manufacturer
            mpn = orderable_part.part_number
            package = orderable_part.package
            physical_properties = orderable_part.properties
            voltage_rating = orderable_part.voltage_rating
            power_rating = orderable_part.power_rating
            model_3d = orderable_part.model_3d
            alternate_mpns = orderable_part.approved_alternate_mpns

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
        self.pads = None if pads is None else _freeze_value(pads)
        self.value = value
        self.manufacturer = manufacturer
        self.mpn = part_number if mpn is None else mpn
        self.package = package
        self.properties = _freeze_value(logical_properties)
        self.physical_properties = None if physical_properties is None else _freeze_value(
            physical_properties
        )
        self.ratings = _freeze_value(ratings or {})
        self.voltage_rating = voltage_rating
        self.power_rating = power_rating
        self.model_3d = model_3d
        self.approved_alternate_mpns = alternate_mpns
        self._orderable_part = normalized_orderable_part
        self.prefix = prefix
        self.extensions = _freeze_value(extensions or {})
        self.source_name = source_name or name
        self.source_version = source_version
        self._library: Library | None = None
        object.__setattr__(self, "_frozen", True)

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
        object.__setattr__(self, "_library", library)

    def _to_part_definition(self) -> _PartDefinition:
        if self._library is None:
            raise ValueError("Part must be added to a Library before instantiation")
        return _PartDefinition(
            name=self.name,
            pins=self.pins,
            properties=_mutable_value(self.properties),
            source_namespace=self._library.namespace,
            source_name=self.source_name,
            source_version=self.source_version or self._library.version,
            physical_part=self._physical_part_spec(),
            prefix=self.prefix,
            schematic_symbols=self.schematic_symbols,
        )

    def _physical_part_spec(self) -> PhysicalPartSpec | None:
        if self._orderable_part is not None:
            return self._orderable_part
        if self.footprint is None:
            return None
        return PhysicalPartSpec(
            manufacturer=self.manufacturer or "",
            part_number=self.mpn or "",
            package=self.package or _default_package(self.footprint),
            footprint=self.footprint,
            pin_pads=None if self.pads is None else _mutable_value(self.pads),
            properties=(
                None
                if self.physical_properties is None
                else _mutable_value(self.physical_properties)
            ),
            voltage_rating=self.voltage_rating,
            power_rating=self.power_rating,
            model_3d=self.model_3d,
            approved_alternate_mpns=self.approved_alternate_mpns,
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
            "properties": _mutable_value(self.properties),
            "physical_properties": (
                None
                if self.physical_properties is None
                else _mutable_value(self.physical_properties)
            ),
            "ratings": _mutable_value(self.ratings),
            "voltage_rating": self.voltage_rating,
            "power_rating": self.power_rating,
            "model_3d": None if self.model_3d is None else self.model_3d._to_dict(),
            "approved_alternate_mpns": list(self.approved_alternate_mpns),
            "prefix": self.prefix,
            "extensions": _mutable_value(self.extensions),
            "source_name": self.source_name,
            "source_version": self.source_version,
        }
        if self.value is not None:
            payload["value"] = self.value
        return payload


def _part_footprint_payload(footprint: FootprintInput | None) -> dict | None:
    if footprint is None:
        return None
    library, name = footprint_ref(footprint)
    result = {"library": library, "name": name}
    if isinstance(footprint, Footprint):
        result["pads"] = [pad._to_dict() for pad in footprint.pads]
    return result


def _part_pads_payload(pads: Mapping[int | str, PinPadValue] | None) -> list[dict[str, object]]:
    if pads is None:
        return []
    return [
        {"pin": str(key), "pads": list(_pad_labels(value))}
        for key, value in sorted(pads.items(), key=lambda item: str(item[0]))
    ]


def _pad_labels(value: PinPadValue) -> tuple[str, ...]:
    if isinstance(value, (tuple, list)):
        return tuple(str(item) for item in value)
    return (str(value),)


def _default_package(footprint: FootprintInput) -> str:
    _library, name = footprint_ref(footprint)
    return name
