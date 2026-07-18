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
class ContractFramedPin:
    """One stable component pin measured relative to another stable pin."""

    key: str
    pin: str
    reference: str

    def _to_dict(self) -> dict[str, object]:
        return {"key": self.key, "pin": self.pin, "reference": self.reference}


@dataclass(frozen=True)
class ContractDirectedRelation:
    """One stable directed relation between two component pins."""

    key: str
    from_pin: str
    to_pin: str

    def _to_dict(self) -> dict[str, object]:
        return {"key": self.key, "from": self.from_pin, "to": self.to_pin}


@dataclass(frozen=True)
class ContractSupplyDomain:
    """One stable supply domain over positive and return pin sets."""

    key: str
    positive_pins: tuple[str, ...]
    return_pins: tuple[str, ...]

    def __post_init__(self) -> None:
        object.__setattr__(self, "positive_pins", tuple(self.positive_pins))
        object.__setattr__(self, "return_pins", tuple(self.return_pins))

    def _to_dict(self) -> dict[str, object]:
        return {
            "key": self.key,
            "positive_pins": list(self.positive_pins),
            "return_pins": list(self.return_pins),
        }


@dataclass(frozen=True)
class ElectricalSubject:
    """Typed reference to a named contract subject or explicit stable pins."""

    kind: str
    key: str | None = None
    positive_pins: tuple[str, ...] = ()
    return_pins: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        object.__setattr__(self, "positive_pins", tuple(self.positive_pins))
        object.__setattr__(self, "return_pins", tuple(self.return_pins))

    @classmethod
    def framed_pin(cls, key: str) -> ElectricalSubject:
        """Reference one named framed-pin subject."""
        return cls("framed_pin", key=key)

    @classmethod
    def directed_relation(cls, key: str) -> ElectricalSubject:
        """Reference one named directed-relation subject."""
        return cls("directed_relation", key=key)

    @classmethod
    def supply_domain(cls, key: str) -> ElectricalSubject:
        """Reference one named supply-domain subject."""
        return cls("supply_domain", key=key)

    @classmethod
    def directed_pins(cls, from_pin: str, to_pin: str) -> ElectricalSubject:
        """Create an explicit directed subject from two stable PinKeys."""
        return cls("directed_pins", positive_pins=(from_pin,), return_pins=(to_pin,))

    @classmethod
    def supply_pins(
        cls, positive_pins: Iterable[str], return_pins: Iterable[str]
    ) -> ElectricalSubject:
        """Create an explicit supply subject from stable PinKey sets."""
        return cls(
            "supply_pins",
            positive_pins=tuple(positive_pins),
            return_pins=tuple(return_pins),
        )

    def _to_dict(self) -> dict[str, object]:
        return {
            "kind": self.kind,
            "key": self.key,
            "positive_pins": list(self.positive_pins),
            "return_pins": list(self.return_pins),
        }


@dataclass(frozen=True)
class FeatureRole:
    """One typed terminal role in a feature schema."""

    key: str
    cardinality: str

    def _to_dict(self) -> dict[str, str]:
        return {"key": self.key, "cardinality": self.cardinality}


@dataclass(frozen=True)
class CanonicalRecordRequirement:
    """One canonical observable/meaning pair required by a feature."""

    observable: str
    meaning: str

    def _to_dict(self) -> dict[str, str]:
        return {"observable": self.observable, "meaning": self.meaning}


@dataclass(frozen=True)
class FeatureSchema:
    """One versioned feature schema over a canonical electrical subject."""

    key: str
    subject_kind: str
    roles: tuple[FeatureRole, ...]
    required_records: tuple[CanonicalRecordRequirement, ...]

    def __post_init__(self) -> None:
        object.__setattr__(self, "roles", tuple(self.roles))
        object.__setattr__(self, "required_records", tuple(self.required_records))

    @classmethod
    def _native_standard(cls, name: str) -> FeatureSchema:
        from . import _volt

        payload = _volt.standard_feature_schema(name)
        return cls(
            payload["key"],
            payload["subject_kind"],
            tuple(FeatureRole(role["key"], role["cardinality"]) for role in payload["roles"]),
            tuple(
                CanonicalRecordRequirement(record["observable"], record["meaning"])
                for record in payload["required_records"]
            ),
        )

    @classmethod
    def supply_consumer(cls) -> FeatureSchema:
        """Return the canonical native supply-consumer feature schema."""
        return cls._native_standard("supply_consumer")

    @classmethod
    def supply_source(cls) -> FeatureSchema:
        """Return the canonical native supply-source feature schema."""
        return cls._native_standard("supply_source")

    @classmethod
    def diode_junction(cls) -> FeatureSchema:
        """Return the canonical native diode-junction feature schema."""
        return cls._native_standard("diode_junction")

    def _to_dict(self) -> dict[str, object]:
        return {
            "key": self.key,
            "subject_kind": self.subject_kind,
            "roles": [role._to_dict() for role in self.roles],
            "required_records": [record._to_dict() for record in self.required_records],
        }


@dataclass(frozen=True)
class FeatureRoleBinding:
    """One feature role assigned to stable component pins."""

    role: str
    pins: tuple[str, ...]

    def __post_init__(self) -> None:
        object.__setattr__(self, "pins", tuple(self.pins))

    def _to_dict(self) -> dict[str, object]:
        return {"role": self.role, "pins": list(self.pins)}


@dataclass(frozen=True)
class FeatureBinding:
    """One component-local binding of a schema to a named subject."""

    key: str
    schema: str
    subject: ElectricalSubject
    roles: tuple[FeatureRoleBinding, ...]

    def __post_init__(self) -> None:
        object.__setattr__(self, "roles", tuple(self.roles))

    def _to_dict(self) -> dict[str, object]:
        return {
            "key": self.key,
            "schema": self.schema,
            "subject": self.subject._to_dict(),
            "roles": [role._to_dict() for role in self.roles],
        }


@dataclass(frozen=True)
class ComponentContract:
    """Complete portable component identity and feature-binding contract."""

    key: str
    pin_keys: tuple[str, ...]
    framed_pins: tuple[ContractFramedPin, ...] = ()
    relations: tuple[ContractDirectedRelation, ...] = ()
    supply_domains: tuple[ContractSupplyDomain, ...] = ()
    feature_schemas: tuple[FeatureSchema, ...] = ()
    feature_bindings: tuple[FeatureBinding, ...] = ()

    def __post_init__(self) -> None:
        for name in (
            "pin_keys",
            "framed_pins",
            "relations",
            "supply_domains",
            "feature_schemas",
            "feature_bindings",
        ):
            object.__setattr__(self, name, tuple(getattr(self, name)))

    def _to_dict(self) -> dict[str, object]:
        return {
            "key": self.key,
            "pin_keys": list(self.pin_keys),
            "framed_pins": [item._to_dict() for item in self.framed_pins],
            "relations": [item._to_dict() for item in self.relations],
            "supply_domains": [item._to_dict() for item in self.supply_domains],
            "feature_schemas": [item._to_dict() for item in self.feature_schemas],
            "feature_bindings": [item._to_dict() for item in self.feature_bindings],
        }


@dataclass(frozen=True)
class ElectricalRecord:
    """One canonical Voltage or Current source record for an exact part."""

    subject: ElectricalSubject
    observable: str
    meaning: str
    value_kind: str
    minimum: float | None = None
    typical: float | None = None
    maximum: float | None = None
    value: float | None = None
    evidence: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        object.__setattr__(self, "evidence", tuple(self.evidence))

    @classmethod
    def accepted_voltage(
        cls, subject: ElectricalSubject, minimum: float, maximum: float
    ) -> ElectricalRecord:
        """Declare an accepted Voltage range on one subject."""
        return cls(subject, "voltage", "accepted_range", "range", minimum, None, maximum)

    @classmethod
    def provided_voltage(
        cls, subject: ElectricalSubject, minimum: float, maximum: float
    ) -> ElectricalRecord:
        """Declare a provided Voltage range on one subject."""
        return cls(subject, "voltage", "provided_range", "range", minimum, None, maximum)

    @classmethod
    def absolute_voltage(
        cls,
        subject: ElectricalSubject,
        *,
        minimum: float | None = None,
        maximum: float | None = None,
    ) -> ElectricalRecord:
        """Declare a one- or two-sided absolute Voltage limit."""
        return cls(subject, "voltage", "absolute_limit", "range", minimum, None, maximum)

    @classmethod
    def characteristic_voltage(
        cls, subject: ElectricalSubject, minimum: float, typical: float, maximum: float
    ) -> ElectricalRecord:
        """Declare a characteristic Voltage envelope."""
        return cls(
            subject, "voltage", "characteristic", "envelope", minimum, typical, maximum
        )

    @classmethod
    def absolute_current(
        cls,
        subject: ElectricalSubject,
        *,
        minimum: float | None = None,
        maximum: float | None = None,
    ) -> ElectricalRecord:
        """Declare a one- or two-sided absolute Current limit."""
        return cls(subject, "current", "absolute_limit", "range", minimum, None, maximum)

    @classmethod
    def current_requirement(
        cls, subject: ElectricalSubject, value: float
    ) -> ElectricalRecord:
        """Declare a continuous Current requirement."""
        return cls(subject, "current", "requirement", "continuous_current", value=value)

    @classmethod
    def current_capability(
        cls, subject: ElectricalSubject, value: float
    ) -> ElectricalRecord:
        """Declare a continuous Current capability."""
        return cls(subject, "current", "capability", "continuous_current", value=value)

    def _to_dict(self) -> dict[str, object]:
        return {
            "subject": self.subject._to_dict(),
            "observable": self.observable,
            "meaning": self.meaning,
            "value_kind": self.value_kind,
            "minimum": self.minimum,
            "typical": self.typical,
            "maximum": self.maximum,
            "value": self.value,
            "evidence": list(self.evidence),
        }


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
    contract: ComponentContract | None = None

    @property
    def schematic_symbol(self) -> SchematicSymbolSpec | None:
        """Return this definition's default schematic symbol, if one is registered."""
        return _schematic_symbol_for_variant(self.schematic_symbols, "default")


class Part:
    """Reusable Python declaration lowered into an exact native library part."""

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
        voltage_rating: float | None = None,
        power_rating: float | None = None,
        contract: ComponentContract | None = None,
        electrical_records: Iterable[ElectricalRecord] = (),
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
        normalized_pins = tuple(pins)
        if symbol is not None and schematic_symbol is not None:
            raise TypeError("Part accepts either symbol or schematic_symbol")
        if mpn is not None and part_number is not None and mpn != part_number:
            raise ValueError("Part mpn and part_number must match when both are provided")
        if orderable is not None:
            if orderable_part is not None:
                raise TypeError("Part accepts either orderable or orderable_part")
            orderable_part = orderable
        same_numbered_pads = False
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
            same_numbered_pads = orderable_part.same_numbered_pads

        if power_rating is not None:
            raise NotImplementedError(
                "Power is not canonical exact-part data in this slice; use a later typed Power record"
            )
        if contract is not None and not isinstance(contract, ComponentContract):
            raise TypeError("Part contract must be a ComponentContract")
        records = tuple(electrical_records)
        if any(not isinstance(record, ElectricalRecord) for record in records):
            raise TypeError("Part electrical_records must contain ElectricalRecord instances")
        if voltage_rating is not None:
            if records:
                raise TypeError(
                    "Part accepts either voltage_rating shorthand or electrical_records"
                )
            if len(normalized_pins) != 2:
                raise ValueError(
                    "voltage_rating shorthand requires an explicitly oriented two-pin part"
                )
            pin_keys = (
                contract.pin_keys
                if contract is not None and len(contract.pin_keys) >= 2
                else ("pin/0", "pin/1")
            )
            records = (
                ElectricalRecord.absolute_voltage(
                    ElectricalSubject.directed_pins(pin_keys[0], pin_keys[1]),
                    maximum=float(voltage_rating),
                ),
            )

        logical_properties = dict(properties or {})
        if value is not None:
            logical_properties["value"] = value

        self.name = name
        self.pins = normalized_pins
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
        self.contract = contract
        self.electrical_records = records
        self.model_3d = model_3d
        self.approved_alternate_mpns = alternate_mpns
        self._same_numbered_pads = same_numbered_pads
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
            contract=self.contract,
        )

    def _physical_part_spec(self) -> PhysicalPartSpec | None:
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
            model_3d=self.model_3d,
            approved_alternate_mpns=self.approved_alternate_mpns,
            same_numbered_pads=self._same_numbered_pads,
        )

    def _has_native_exact_definition(self) -> bool:
        """Return whether this declaration can form a complete native P3 part."""
        physical = self._physical_part_spec()
        return bool(
            self.pins
            and isinstance(self.footprint, Footprint)
            and physical is not None
            and physical.manufacturer
            and physical.part_number
            and physical.package
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
            "contract": None if self.contract is None else self.contract._to_dict(),
            "electrical_records": [record._to_dict() for record in self.electrical_records],
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
        if footprint.courtyard is not None:
            result["courtyard"] = list(footprint.courtyard)
        if footprint.body is not None:
            result["body"] = list(footprint.body)
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
