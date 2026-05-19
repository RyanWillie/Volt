"""Python authoring surface for Volt logical circuits."""

from __future__ import annotations

import json
from contextlib import contextmanager
from dataclasses import dataclass
from math import isfinite
from pathlib import Path
from typing import Iterable, Iterator

from . import _volt


@dataclass(frozen=True)
class DiagnosticEntity:
    """Reference to a kernel entity involved in a diagnostic."""

    kind: str
    index: int


@dataclass(frozen=True)
class Diagnostic:
    """Kernel-produced diagnostic exposed through the Python facade."""

    severity: str
    code: str
    message: str
    entities: tuple[DiagnosticEntity, ...]


class DiagnosticReport:
    """Ordered collection of kernel diagnostics."""

    def __init__(self, diagnostics: Iterable[Diagnostic]):
        self._diagnostics = tuple(diagnostics)

    def __iter__(self) -> Iterator[Diagnostic]:
        return iter(self._diagnostics)

    def __len__(self) -> int:
        return len(self._diagnostics)

    def __getitem__(self, index: int) -> Diagnostic:
        return self._diagnostics[index]

    @property
    def has_errors(self) -> bool:
        return any(diagnostic.severity == "error" for diagnostic in self._diagnostics)


@dataclass(frozen=True)
class TemplateNetInfo:
    """Read-only view of a module template-local net."""

    index: int
    name: str
    kind: str


@dataclass(frozen=True)
class PortInfo:
    """Read-only view of a module port definition."""

    index: int
    name: str
    internal_net: int
    role: str
    required: bool


@dataclass(frozen=True)
class ModuleComponentInfo:
    """Read-only view of a component template inside a module definition."""

    index: int
    definition: int
    reference: str


@dataclass(frozen=True)
class ModuleConnectionInfo:
    """Read-only view of a module-local pin-to-net connection."""

    net: int
    component: int
    pin_definition: int


@dataclass(frozen=True)
class ModuleNetOriginInfo:
    """Read-only view of a concrete net created from a module template net."""

    template_net: int
    net: int


@dataclass(frozen=True)
class ModuleComponentOriginInfo:
    """Read-only view of a concrete component created from a module component template."""

    module_component: int
    component: int


@dataclass(frozen=True)
class PortBindingInfo:
    """Read-only view of a module instance port binding edge."""

    port: int
    internal_net: int
    parent_net: int


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
    def line(start: tuple[float, float], end: tuple[float, float]) -> dict:
        return {"type": "line", "start": _symbol_point(start), "end": _symbol_point(end)}

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


class ComponentDefinition:
    """Handle to a kernel-owned reusable component definition."""

    def __init__(self, design: Design, index: int, name: str):
        self._design = design
        self._index = index
        self.name = name

    @property
    def index(self) -> int:
        return self._index

    def __repr__(self) -> str:
        return f"ComponentDefinition(name={self.name!r}, index={self._index})"


class ModuleDefinition:
    """Handle to a kernel-owned reusable module definition."""

    def __init__(self, design: Design, index: int, name: str):
        self._design = design
        self._index = index
        self.name = name
        self._ports_by_name: dict[str, int] = {}
        self._components_by_ref: dict[str, int] = {}

    @property
    def index(self) -> int:
        return self._index

    def net(self, name: str, *, kind: str = "signal") -> ModuleNet:
        net = self._design._circuit.add_template_net(self._index, name, kind)
        return ModuleNet(self, net, name)

    def port(
        self,
        name: str,
        *,
        kind: str = "signal",
        role: str = "passive",
        required: bool = True,
    ) -> ModulePort:
        net = self._design._circuit.add_template_net(self._index, name, kind)
        port = self._design._circuit.add_port(self._index, name, net, role, required)
        self._ports_by_name[name] = port
        return ModulePort(self, net, port, name)

    def instantiate(
        self,
        definition: ComponentDefinition | LibraryComponent,
        *,
        ref: str,
        properties: dict | None = None,
    ) -> ModuleComponent:
        if isinstance(definition, LibraryComponent):
            if definition.physical_part is not None:
                raise ValueError("Module library components do not support selected physical parts")
            definition = self._design._define_library_component(definition)
        if not isinstance(definition, ComponentDefinition):
            raise TypeError("Module instantiate expects a ComponentDefinition handle")
        if definition._design is not self._design:
            raise ValueError("Component definition belongs to a different design")
        component = self._design._circuit.add_module_component(
            self._index, definition.index, ref, properties or {}
        )
        self._components_by_ref[ref] = component
        return ModuleComponent(self, component, ref)

    def connect(self, *endpoints) -> ModuleNet:
        return _connect_module_endpoints(_flatten_module_endpoints(endpoints))

    def template_nets(self) -> tuple[TemplateNetInfo, ...]:
        return tuple(
            TemplateNetInfo(item["index"], item["name"], item["kind"])
            for item in self._design._circuit.template_nets(self._index)
        )

    def ports(self) -> tuple[PortInfo, ...]:
        return tuple(
            PortInfo(
                item["index"],
                item["name"],
                item["internal_net"],
                item["role"],
                item["required"],
            )
            for item in self._design._circuit.module_ports(self._index)
        )

    def components(self) -> tuple[ModuleComponentInfo, ...]:
        return tuple(
            ModuleComponentInfo(item["index"], item["definition"], item["reference"])
            for item in self._design._circuit.module_components(self._index)
        )

    def connections(self) -> tuple[ModuleConnectionInfo, ...]:
        return tuple(
            ModuleConnectionInfo(item["net"], item["component"], item["pin_definition"])
            for item in self._design._circuit.module_connections(self._index)
        )

    def __repr__(self) -> str:
        return f"ModuleDefinition(name={self.name!r}, index={self._index})"


class ModuleNet:
    """Handle to a template-local module net."""

    def __init__(self, module: ModuleDefinition, index: int, name: str):
        self._module = module
        self._index = index
        self.name = name

    @property
    def index(self) -> int:
        return self._index

    def connect(self, *pins: ModulePin | Iterable[ModulePin]) -> ModuleNet:
        return _connect_module_endpoints((self, *_flatten_module_endpoints(pins)))

    def __iadd__(self, pins: ModulePin | Iterable[ModulePin]) -> ModuleNet:
        return self.connect(pins)

    def __repr__(self) -> str:
        return f"ModuleNet(name={self.name!r}, index={self._index})"


class ModulePort(ModuleNet):
    """Handle to a module boundary port and its internal template net."""

    def __init__(self, module: ModuleDefinition, net_index: int, port_index: int, name: str):
        super().__init__(module, net_index, name)
        self._port_index = port_index

    @property
    def port_index(self) -> int:
        return self._port_index

    def __repr__(self) -> str:
        return (
            f"ModulePort(name={self.name!r}, net_index={self._index}, "
            f"port_index={self._port_index})"
        )


class ModulePin:
    """Handle to a reusable pin definition on a module-local component template."""

    def __init__(self, component: ModuleComponent, index: int):
        self._component = component
        self._index = index

    @property
    def index(self) -> int:
        return self._index

    def __repr__(self) -> str:
        return f"ModulePin(index={self._index})"


class ModulePinGroup:
    """Repeated-label module component pins selected as a group."""

    def __init__(self, component: ModuleComponent, name: str, pins: Iterable[ModulePin]):
        self._component = component
        self.name = name
        self._pins = tuple(pins)

    def __iter__(self) -> Iterator[ModulePin]:
        return iter(self._pins)

    def __len__(self) -> int:
        return len(self._pins)

    def __getitem__(self, index: int) -> ModulePin:
        return self._pins[index]

    def __repr__(self) -> str:
        return f"ModulePinGroup(name={self.name!r}, pins={self._pins!r})"


class ModuleComponent:
    """Handle to a component occurrence inside a reusable module definition."""

    def __init__(self, module: ModuleDefinition, index: int, reference: str):
        self._module = module
        self._index = index
        self.reference = reference

    @property
    def index(self) -> int:
        return self._index

    def __getitem__(self, key: int | str) -> ModulePin:
        if isinstance(key, int):
            pin = self._module._design._circuit.module_component_pin_by_number(
                self._index, str(key)
            )
        elif isinstance(key, str):
            pin = _resolve_single_pin_ref(
                self._pin_refs(),
                key,
                missing_message="Module component has no pin with that name",
                ambiguous_message=(
                    f"Module component pin name {key!r} is ambiguous; use pins({key!r}) "
                    "for the group or address one physical pin by number"
                ),
            )
        else:
            raise TypeError("Module component pins are addressed by int number or str name")

        return ModulePin(self, pin)

    def pins(self, name: str) -> ModulePinGroup:
        if not isinstance(name, str):
            raise TypeError("Module component pin groups are addressed by str name")
        matches = _pin_refs_by_name(self._pin_refs(), name)
        if not matches:
            raise IndexError("Module component has no pin with that name")
        return ModulePinGroup(self, name, (ModulePin(self, item["index"]) for item in matches))

    def _pin_refs(self):
        return self._module._design._circuit.module_component_pin_refs(self._index)

    def __repr__(self) -> str:
        return f"ModuleComponent(reference={self.reference!r}, index={self._index})"


class ModuleInstancePort:
    """Handle to a root module instance port endpoint."""

    def __init__(self, instance: ModuleInstance, port_index: int, name: str):
        self._instance = instance
        self._port_index = port_index
        self.name = name

    @property
    def port_index(self) -> int:
        return self._port_index

    def __repr__(self) -> str:
        return f"ModuleInstancePort(name={self.name!r}, port_index={self._port_index})"


class ModuleInstance:
    """Handle to a kernel-owned root module instance."""

    def __init__(self, design: Design, definition: ModuleDefinition, index: int, name: str):
        self._design = design
        self._definition = definition
        self._index = index
        self.name = name
        self._ports_by_name: dict[str, int] = {}
        self._components_by_ref: dict[str, int] = {}

    @property
    def index(self) -> int:
        return self._index

    def __getitem__(self, key: str) -> ModuleInstancePort:
        if not isinstance(key, str):
            raise TypeError("Module instance ports are addressed by str name")
        if key not in self._ports_by_name:
            raise KeyError(key)
        return ModuleInstancePort(self, self._ports_by_name[key], key)

    def component(self, ref: str) -> Component:
        if ref not in self._components_by_ref:
            raise KeyError(ref)
        return Component(self._design, self._components_by_ref[ref])

    def net_origins(self) -> tuple[ModuleNetOriginInfo, ...]:
        return tuple(
            ModuleNetOriginInfo(item["template_net"], item["net"])
            for item in self._design._circuit.module_net_origins(self._index)
        )

    def component_origins(self) -> tuple[ModuleComponentOriginInfo, ...]:
        return tuple(
            ModuleComponentOriginInfo(item["module_component"], item["component"])
            for item in self._design._circuit.module_component_origins(self._index)
        )

    def port_bindings(self) -> tuple[PortBindingInfo, ...]:
        return tuple(
            PortBindingInfo(item["port"], item["internal_net"], item["parent_net"])
            for item in self._design._circuit.port_bindings(self._index)
        )

    def __repr__(self) -> str:
        return f"ModuleInstance(name={self.name!r}, index={self._index})"


class Pin:
    """Handle to a kernel-owned concrete pin."""

    def __init__(self, design: Design, index: int):
        self._design = design
        self._index = index

    @property
    def index(self) -> int:
        return self._index

    def mark_no_connect(self) -> Pin:
        self._design._circuit.mark_intentional_no_connect_pin(self._index)
        return self

    def __repr__(self) -> str:
        return f"Pin(index={self._index})"


class PinGroup:
    """Repeated-label concrete pins selected as a group."""

    def __init__(self, component: Component, name: str, pins: Iterable[Pin]):
        self._component = component
        self.name = name
        self._pins = tuple(pins)

    def __iter__(self) -> Iterator[Pin]:
        return iter(self._pins)

    def __len__(self) -> int:
        return len(self._pins)

    def __getitem__(self, index: int) -> Pin:
        return self._pins[index]

    def __repr__(self) -> str:
        return f"PinGroup(name={self.name!r}, pins={self._pins!r})"


class Component:
    """Handle to a kernel-owned component instance."""

    def __init__(self, design: Design, index: int):
        self._design = design
        self._index = index

    @property
    def index(self) -> int:
        return self._index

    @property
    def reference(self) -> str:
        return self._design._circuit.component_reference(self._index)

    @property
    def schematic_symbol(self) -> SchematicSymbolSpec | str | None:
        return self.schematic_symbol_variant("default")

    def schematic_symbol_variant(self, variant: str) -> SchematicSymbolSpec | str | None:
        if not isinstance(variant, str):
            raise TypeError("schematic symbol variant must be a string")
        if not variant:
            raise ValueError("schematic symbol variant must not be empty")
        name = self._design._circuit.component_schematic_symbol(self._index, variant)
        if name is None:
            return None
        return self._design._schematic_symbols.get(name, name)

    def __getitem__(self, key: int | str) -> Pin:
        if isinstance(key, int):
            pin = self._design._circuit.pin_by_number(self._index, str(key))
        elif isinstance(key, str):
            pin = _resolve_single_pin_ref(
                self._pin_refs(),
                key,
                missing_message=f"Component {self.reference} has no pin with that name",
                ambiguous_message=(
                    f"Component {self.reference} pin name {key!r} is ambiguous; use pins({key!r}) "
                    "for the group or address one physical pin by number"
                ),
            )
        else:
            raise TypeError("Component pins are addressed by int number or str name")

        return Pin(self._design, pin)

    def pins(self, name: str) -> PinGroup:
        if not isinstance(name, str):
            raise TypeError("Component pin groups are addressed by str name")
        matches = _pin_refs_by_name(self._pin_refs(), name)
        if not matches:
            raise IndexError("Component has no pin with that name")
        return PinGroup(self, name, (Pin(self._design, item["index"]) for item in matches))

    def _pin_refs(self):
        return self._design._circuit.pin_refs(self._index)

    def select_part(
        self,
        *,
        manufacturer: str,
        part_number: str,
        package: str,
        footprint: tuple[str, str],
        pin_pads: dict[int | str, str],
        properties: dict | None = None,
        voltage_rating: float | None = None,
        power_rating: float | None = None,
    ) -> Component:
        if not isinstance(pin_pads, dict):
            raise TypeError("pin_pads must be a dict")
        if not isinstance(footprint, tuple) or len(footprint) != 2:
            raise TypeError("footprint must be a (library, name) tuple")

        selected_part_ratings = []
        if voltage_rating is not None:
            selected_part_ratings.append(("voltage_rating", "voltage", _number(voltage_rating)))
        if power_rating is not None:
            selected_part_ratings.append(("power_rating", "power", _number(power_rating)))

        self._design._circuit.select_physical_part(
            self._index,
            manufacturer,
            part_number,
            package,
            footprint[0],
            footprint[1],
            pin_pads,
            properties or {},
        )
        for name, dimension, value in selected_part_ratings:
            self._design._circuit.set_selected_part_quantity(self._index, name, dimension, value)
        return self

    def __repr__(self) -> str:
        return f"Component(index={self._index})"


class Net:
    """Handle to a kernel-owned logical net."""

    def __init__(self, design: Design, index: int, name: str):
        self._design = design
        self._index = index
        self.name = name

    @property
    def index(self) -> int:
        return self._index

    def mark_stub(self) -> Net:
        self._design._circuit.mark_intentional_stub_net(self._index)
        return self

    def pins(self) -> tuple[Pin, ...]:
        return tuple(
            Pin(self._design, index) for index in self._design._circuit.net_pins(self._index)
        )

    def connect(self, *pins: Pin | ModuleInstancePort | Iterable[Pin | ModuleInstancePort]) -> Net:
        for pin in _flatten_pins(pins):
            if isinstance(pin, Pin):
                self._design._circuit.connect(self._index, pin.index)
            elif isinstance(pin, ModuleInstancePort):
                if pin._instance._design is not self._design:
                    raise ValueError("Module instance port belongs to a different design")
                self._design._circuit.bind_port(
                    pin._instance.index, pin.port_index, self._index
                )
            else:
                raise TypeError("Nets can only connect Pin or ModuleInstancePort handles")
        return self

    def __iadd__(self, pins: Pin | ModuleInstancePort | Iterable[Pin | ModuleInstancePort]) -> Net:
        return self.connect(pins)

    def __repr__(self) -> str:
        return f"Net(name={self.name!r}, index={self._index})"


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
    """Handle to a placed power, ground, sheet, or off-page schematic port."""

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
        loc: str = "top",
        name: str = "label",
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
    ) -> PlacedSchematicElement:
        if not isinstance(text, str):
            raise TypeError("Schematic element labels must be strings")
        if not text:
            raise ValueError("Schematic element labels must not be empty")
        if not isinstance(name, str):
            raise TypeError("Schematic element label field names must be strings")
        if not name:
            raise ValueError("Schematic element label field names must not be empty")
        at = _element_label_point(self, loc, _label_offset(offset, ofst))
        self.symbol._schematic._add_symbol_field(
            self.symbol,
            name=name,
            value=text,
            at=at,
            orient=self.orientation if orient is None else orient,
            _authored_region=self.symbol._authored_region,
        )
        return self

    def label_ref(
        self,
        *,
        loc: str = "top",
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
    ) -> PlacedSchematicElement:
        return self.label(
            self.component.reference,
            loc=loc,
            name="reference",
            offset=offset,
            ofst=ofst,
            orient=orient,
        )

    def label_value(
        self,
        *,
        loc: str = "bottom",
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
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
        )

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
        return sorted(result)

    def __repr__(self) -> str:
        return f"PlacedSchematicElement(symbol={self.symbol!r})"


class SchematicWire:
    """Read-only handle to a schematic wire run projection."""

    def __init__(self, schematic: Schematic, index: int):
        self._schematic = schematic
        self._index = index

    @property
    def index(self) -> int:
        return self._index

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
        self._drawing = drawing
        self._authored_region = authored_region
        self._start_here = drawing.here if drawing is not None else None
        self._start_direction = drawing.direction if drawing is not None else None
        self._wire: SchematicWire | None = None

    def at(self, point) -> SchematicWireBuilder:
        return self.from_(point)

    def from_(self, point) -> SchematicWireBuilder:
        self._require_unmaterialized()
        self._points = [self._point_for_authoring(point)]
        self._update_drawing_cursor(self._points[-1])
        return self

    def via(self, point) -> SchematicWireBuilder:
        """Append an explicit intermediate point that the route should preserve."""
        self._require_unmaterialized()
        self._require_started()
        self._append_point(self._point_for_authoring(point))
        return self

    def to(self, point, *, shape: str | None = None, k: float | None = None):
        """Append the next route point, normally the terminal endpoint."""
        self._require_unmaterialized()
        self._require_started()
        self._append_point(self._point_for_authoring(point))
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
                )
            except Exception:
                self._clear_pending()
                self._restore_drawing_state()
                raise
        self._clear_pending()
        return self._wire

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

    def R(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        reference_label: str | None = None,
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(
            component, symbol=symbol, variant=variant, reference_label=reference_label
        )

    def C(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        reference_label: str | None = None,
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(
            component, symbol=symbol, variant=variant, reference_label=reference_label
        )

    def L(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        reference_label: str | None = None,
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(
            component, symbol=symbol, variant=variant, reference_label=reference_label
        )

    def D(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        reference_label: str | None = None,
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(
            component, symbol=symbol, variant=variant, reference_label=reference_label
        )

    def LED(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
        reference_label: str | None = None,
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(
            component, symbol=symbol, variant=variant, reference_label=reference_label
        )

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

    def connect(
        self,
        start: tuple[float, float] | SchematicAnchor | SchematicPort,
        end: tuple[float, float] | SchematicAnchor | SchematicPort,
        *,
        net: Net | None = None,
        shape: str | None = None,
        k: float | None = None,
    ) -> SchematicWire:
        self._flush_pending()
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

    def power(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        net: Net | None = None,
        orient: str = "Up",
    ) -> SchematicPort:
        self._flush_pending()
        return self._schematic.power(
            name,
            at=self._here if at is None else self._point_arg(at),
            net=net,
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
        self._flush_pending()
        return self._schematic.ground(
            name,
            at=self._here if at is None else self._point_arg(at),
            net=net,
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
        reason: str | None = None,
    ) -> SchematicNoConnect:
        self._flush_pending()
        return self._schematic.no_connect(
            anchor,
            orient=orient,
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
        loc: str = "top",
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
        loc: str = "top",
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
        loc: str = "bottom",
        offset: float | None = None,
        ofst: float | None = None,
        orient: str | None = None,
    ) -> PlacedSchematicElement:
        return self._materialize().label_value(
            loc=loc, offset=offset, ofst=ofst, orient=orient
        )

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
                "flip",
                "index",
                "label",
                "label_ref",
                "label_value",
                "left",
                "length",
                "orientation",
                "pin",
                "pin_anchor",
                "pin_anchors",
                "pins",
                "reverse",
                "right",
                "start",
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
            reference_label,
        )
        return SchematicSymbol(self, instance, component, orientation, _authored_region)

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
        start: tuple[float, float] | SchematicAnchor | SchematicPort,
        end: tuple[float, float] | SchematicAnchor | SchematicPort,
        *,
        net: Net | None = None,
        shape: str | None = None,
        k: float | None = None,
        _authored_region: int | None = None,
    ) -> SchematicWire:
        resolved_net = _resolve_schematic_connection_net(
            self._design,
            start,
            end,
            net,
            schematic=self,
        )
        builder = self.wire(resolved_net, _authored_region=_authored_region).from_(start).to(end)
        if shape is None:
            return builder.orthogonal()
        return builder.shape(shape, k=k)

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

    def _add_wire(
        self,
        net: Net,
        points: Iterable[tuple[float, float]],
        *,
        route_intent: str,
        _authored_region: int | None = None,
    ) -> SchematicWire:
        wire_points = tuple(points)
        if len(wire_points) < 2:
            raise ValueError("Schematic wires need at least two points")

        converted = []
        for point in wire_points:
            converted.append(
                _schematic_point_for_authoring(
                    point,
                    design=self._design,
                    schematic=self,
                    action="schematic wire",
                )
            )

        wire = self._design._circuit.add_schematic_wire(
            self._sheet_index, net.index, converted, route_intent, _authored_region
        )
        return SchematicWire(self, wire)

    def label(
        self,
        net: Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
        label: str | None = None,
        _authored_region: int | None = None,
    ) -> SchematicNetLabel:
        if not isinstance(net, Net):
            raise TypeError("Schematic labels expect a Net handle")
        if net._design is not self._design:
            raise ValueError(
                _with_schematic_context(_cross_design_net_message(net), self, "net label")
            )
        x, y = _schematic_point_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="net label",
        )
        orientation = _orientation(orient)

        label = self._design._circuit.add_schematic_net_label(
            self._sheet_index,
            net.index,
            x,
            y,
            orientation,
            _authored_region,
            _optional_display_label(label),
        )
        return SchematicNetLabel(self, label, orientation)

    def net_label(
        self,
        name_or_net: str | Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
        label: str | None = None,
        _authored_region: int | None = None,
    ) -> SchematicNetLabel:
        try:
            net = _resolve_schematic_net_label(self._design, name_or_net)
        except ValueError as error:
            _raise_cross_design_with_context(error, self, "net label")
        return self.label(net, at=at, orient=orient, label=label, _authored_region=_authored_region)

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
        net = _resolve_schematic_signal_net(
            self._design,
            name_or_net,
            at,
            schematic=self,
            action="local net label",
        )
        anchor = _schematic_point_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="local net label",
        )
        position = _offset_schematic_point(
            anchor,
            side_orientation,
            _nonnegative_coordinate(offset, "Local net label offsets"),
        )
        return self.label(
            net,
            at=position,
            orient=label_orientation,
            _authored_region=_authored_region,
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
        net = _resolve_schematic_signal_net(
            self._design,
            name_or_net,
            at,
            schematic=self,
            action="signal stub",
        )
        start = _schematic_point_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="signal stub",
        )
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
        wire = self._add_wire(
            net,
            (start, end),
            route_intent="Direct",
            _authored_region=_authored_region,
        )
        label = self.label(
            net,
            at=label_position,
            orient=label_orientation,
            label=label,
            _authored_region=_authored_region,
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
            self._sheet_index, symbol.index, name, value, x, y, orientation, _authored_region
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
        x, y = _schematic_point_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="junction",
        )
        junction = self._design._circuit.add_schematic_junction(
            self._sheet_index, net.index, x, y, _authored_region
        )
        return SchematicJunction(self, junction)

    def power(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Up",
        _authored_region: int | None = None,
    ) -> SchematicPort:
        net = _resolve_schematic_port_net(
            self._design,
            at,
            net,
            schematic=self,
            action="power port",
        )
        return self._power_port(
            name,
            net=net,
            at=at,
            orient=orient,
            kind="Power",
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
        net = _resolve_schematic_port_net(
            self._design,
            at,
            net,
            schematic=self,
            action="ground port",
        )
        return self._power_port(
            net.name if name is None else name,
            net=net,
            at=at,
            orient=orient,
            kind="Ground",
            _authored_region=_authored_region,
        )

    def _power_port(
        self,
        name: str,
        *,
        net: Net,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str,
        kind: str,
        _authored_region: int | None = None,
    ) -> SchematicPort:
        if not isinstance(name, str):
            raise TypeError("Schematic power port names must be strings")
        if not name:
            raise ValueError("Schematic power port names must not be empty")
        if not isinstance(net, Net):
            raise TypeError("Schematic power ports expect a Net handle")
        if net._design is not self._design:
            raise ValueError(
                _with_schematic_context(_cross_design_net_message(net), self, "power port")
            )
        x, y = _schematic_point_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="power port",
        )
        orientation = _orientation(orient)
        port = self._design._circuit.add_schematic_power_port(
            self._sheet_index,
            net.index,
            kind,
            x,
            y,
            orientation,
            _authored_region,
            None if name == net.name else name,
        )
        return SchematicPort(
            self,
            port,
            net=net,
            name=name,
            kind=kind,
            at=(x, y),
            orientation=orientation,
        )

    def no_connect(
        self,
        pin: SchematicPinAnchor,
        *,
        orient: str = "Right",
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
        marker = self._design._circuit.add_schematic_no_connect_marker(
            self._sheet_index,
            pin.pin.index,
            pin.x,
            pin.y,
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
        net = _resolve_schematic_sheet_port_net(
            self._design,
            name,
            at,
            net,
            schematic=self,
        )
        x, y = _schematic_point_for_authoring(
            at,
            design=self._design,
            schematic=self,
            action="sheet port",
        )
        orientation = _orientation(orient)
        port_kind = _sheet_port_kind(kind)
        port = self._design._circuit.add_schematic_sheet_port(
            self._sheet_index, net.index, name, port_kind, x, y, orientation, _authored_region
        )
        return SchematicPort(
            self,
            port,
            net=net,
            name=name,
            kind=port_kind,
            at=(x, y),
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

    def power(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Up",
    ) -> SchematicPort:
        return self._sheet.power(
            name,
            at=self._local_point(at),
            net=net,
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
        return self._sheet.ground(
            name,
            at=self._local_point(at),
            net=net,
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
        reason: str | None = None,
    ) -> SchematicNoConnect:
        return self._sheet.no_connect(
            anchor,
            orient=orient,
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


class Design:
    """Root Python handle for one kernel-owned logical circuit."""

    def __init__(self, name: str):
        if not name:
            raise ValueError("Design name must not be empty")

        self.name = name
        self._circuit = _volt.Circuit()
        self._definitions: dict[str, int] = {}
        self._library_definitions: dict[tuple[str, str, str], ComponentDefinition] = {}
        self._schematic_symbols: dict[str, SchematicSymbolSpec] = {}
        self._schematic_sheets: dict[str, int] = {}

    def net(self, name: str, *, kind: str = "signal", voltage: float | None = None) -> Net:
        net = Net(self, self._circuit.add_net(name, kind), name)
        if voltage is not None:
            self._circuit.set_net_quantity(net.index, "voltage", "voltage", _number(voltage))
        return net

    def nets(self) -> tuple[Net, ...]:
        return tuple(Net(self, item["index"], item["name"]) for item in self._circuit.net_refs())

    def R(
        self,
        value: str | None = None,
        *,
        resistance: float | None = None,
        tolerance: float | None = None,
        ref: str | None = None,
    ) -> Component:
        properties = {}
        if value is not None:
            properties["value"] = value
        component = self._instantiate(
            "resistor", self._circuit.define_resistor, "R", ref, properties
        )
        if resistance is not None:
            self._circuit.set_component_quantity(
                component.index, "resistance", "resistance", _number(resistance)
            )
        if tolerance is not None:
            self._circuit.set_component_percent_tolerance(component.index, _number(tolerance))
        return component

    def C(
        self,
        value: str | None = None,
        *,
        capacitance: float | None = None,
        voltage_rating: float | None = None,
        ref: str | None = None,
    ) -> Component:
        properties = {}
        if value is not None:
            properties["value"] = value
        component = self._instantiate(
            "capacitor", self._circuit.define_capacitor, "C", ref, properties
        )
        if capacitance is not None:
            self._circuit.set_component_quantity(
                component.index, "capacitance", "capacitance", _number(capacitance)
            )
        if voltage_rating is not None:
            voltage_rating_value = _number(voltage_rating)
            self._circuit.select_generic_physical_part(component.index)
            self._circuit.set_selected_part_quantity(
                component.index, "voltage_rating", "voltage", voltage_rating_value
            )
        return component

    def CP(
        self,
        value: str | None = None,
        *,
        capacitance: float | None = None,
        voltage_rating: float | None = None,
        ref: str | None = None,
    ) -> Component:
        properties = {}
        if value is not None:
            properties["value"] = value
        component = self._instantiate(
            "polarized_capacitor",
            self._circuit.define_polarized_capacitor,
            "C",
            ref,
            properties,
        )
        if capacitance is not None:
            self._circuit.set_component_quantity(
                component.index, "capacitance", "capacitance", _number(capacitance)
            )
        if voltage_rating is not None:
            voltage_rating_value = _number(voltage_rating)
            self._circuit.select_generic_physical_part(component.index)
            self._circuit.set_selected_part_quantity(
                component.index, "voltage_rating", "voltage", voltage_rating_value
            )
        return component

    def L(
        self,
        value: str | None = None,
        *,
        inductance: float | None = None,
        ref: str | None = None,
    ) -> Component:
        properties = {}
        if value is not None:
            properties["value"] = value
        component = self._instantiate(
            "inductor", self._circuit.define_inductor, "L", ref, properties
        )
        if inductance is not None:
            self._circuit.set_component_quantity(
                component.index, "inductance", "inductance", _number(inductance)
            )
        return component

    def define_component(
        self,
        name: str,
        *,
        pins: Iterable[PinSpec],
        properties: dict | None = None,
        source: tuple[str, str, str] | None = None,
        schematic_symbol: SchematicSymbolSpec | Iterable[SchematicSymbolSpec] | None = None,
    ) -> ComponentDefinition:
        schematic_symbols = _normalize_schematic_symbols(schematic_symbol)
        for symbol in schematic_symbols:
            self._register_schematic_symbol(symbol)

        source_namespace = ""
        source_name = ""
        source_version = ""
        if source is not None:
            if not isinstance(source, tuple) or len(source) != 3:
                raise TypeError("source must be a (namespace, name, version) tuple")
            source_namespace, source_name, source_version = source
            for value, label in (
                (source_namespace, "namespace"),
                (source_name, "name"),
                (source_version, "version"),
            ):
                if not isinstance(value, str):
                    raise TypeError(f"source {label} must be a string")
                if not value:
                    raise ValueError(f"source {label} must not be empty")
        definition = self._circuit.define_component(
            name,
            [pin._to_dict() for pin in pins],
            properties or {},
            source_namespace,
            source_name,
            source_version,
            _schematic_symbol_refs(schematic_symbols),
        )
        return ComponentDefinition(self, definition, name)

    def define_module(self, name: str) -> ModuleDefinition:
        module = self._circuit.define_module(name)
        return ModuleDefinition(self, module, name)

    def instantiate(
        self,
        definition: ComponentDefinition | ModuleDefinition | LibraryComponent,
        *,
        ref: str | None = None,
        prefix: str | None = None,
        properties: dict | None = None,
    ) -> Component | ModuleInstance:
        if isinstance(definition, LibraryComponent):
            component_definition = self._define_library_component(definition)
            component = self.instantiate(
                component_definition,
                ref=ref,
                prefix=definition.prefix if prefix is None else prefix,
                properties=properties,
            )
            if definition.physical_part is not None:
                part = definition.physical_part
                component.select_part(
                    manufacturer=part.manufacturer,
                    part_number=part.part_number,
                    package=part.package,
                    footprint=part.footprint,
                    pin_pads=part.pin_pads_for(definition),
                    properties=part.properties,
                    voltage_rating=part.voltage_rating,
                    power_rating=part.power_rating,
                )
            return component

        if isinstance(definition, ModuleDefinition):
            if definition._design is not self:
                raise ValueError("Module definition belongs to a different design")
            if ref is None:
                raise ValueError("Module instances require an explicit ref")
            instance = self._circuit.instantiate_root_module(definition.index, ref)
            result = ModuleInstance(self, definition, instance, ref)
            for name, port in definition._ports_by_name.items():
                result._ports_by_name[name] = port
            for reference, component in definition._components_by_ref.items():
                result._components_by_ref[reference] = self._circuit.concrete_component_for(
                    instance, component
                )
            return result

        if not isinstance(definition, ComponentDefinition):
            raise TypeError("instantiate expects a ComponentDefinition or ModuleDefinition handle")
        if definition._design is not self:
            raise ValueError("Component definition belongs to a different design")
        if prefix is None:
            prefix = "U"
        if ref is None:
            component = self._circuit.instantiate_auto(definition.index, prefix, properties or {})
        else:
            component = self._circuit.instantiate_ref(definition.index, ref, properties or {})
        return Component(self, component)

    def _define_library_component(self, component: LibraryComponent) -> ComponentDefinition:
        if component.cache_key not in self._library_definitions:
            self._library_definitions[component.cache_key] = self.define_component(
                component.name,
                pins=component.pins,
                properties=component.properties,
                source=(
                    component.library.namespace,
                    component.source_name,
                    component.source_version,
                ),
                schematic_symbol=component.schematic_symbols,
            )
        return self._library_definitions[component.cache_key]

    def _register_schematic_symbol(self, symbol: SchematicSymbolSpec) -> None:
        self._circuit.register_schematic_symbol(symbol._to_dict())
        self._schematic_symbols[symbol.name] = symbol

    def LED(self, *, ref: str | None = None) -> Component:
        return self._instantiate("led", self._circuit.define_led, "D", ref, {})

    def diode(self, *, ref: str | None = None) -> Component:
        return self._instantiate("diode", self._circuit.define_diode, "D", ref, {})

    def switch(self, *, ref: str | None = None) -> Component:
        return self._instantiate("switch_spst", self._circuit.define_switch_spst, "SW", ref, {})

    def crystal(self, *, ref: str | None = None) -> Component:
        return self._instantiate("crystal_2pin", self._circuit.define_crystal_2pin, "Y", ref, {})

    def test_point(self, *, ref: str | None = None) -> Component:
        return self._instantiate("test_point", self._circuit.define_test_point, "TP", ref, {})

    def connector_1x01(self, *, ref: str | None = None) -> Component:
        return self._instantiate(
            "connector_1x01", self._circuit.define_connector_1x01, "J", ref, {}
        )

    def connector_1x02(self, *, ref: str | None = None) -> Component:
        return self._instantiate(
            "connector_1x02", self._circuit.define_connector_1x02, "J", ref, {}
        )

    def connector_1x03(self, *, ref: str | None = None) -> Component:
        return self._instantiate(
            "connector_1x03", self._circuit.define_connector_1x03, "J", ref, {}
        )

    def regulator(self, *, ref: str | None = None) -> Component:
        return self._instantiate(
            "regulator_3pin", self._circuit.define_regulator_3pin, "U", ref, {}
        )

    def op_amp(self, *, ref: str | None = None) -> Component:
        return self._instantiate("op_amp_5pin", self._circuit.define_op_amp_5pin, "U", ref, {})

    def schematic(
        self,
        name: str,
        *,
        size: str | tuple[float, float] | dict | None = None,
        orientation: str | None = None,
        title: str | None = None,
        number: str | int | None = None,
        page_count: str | int | None = None,
        revision: str | None = None,
        date: str | None = None,
        project: str | None = None,
        file: str | None = None,
        title_block: dict[str, str] | Iterable[tuple[str, str] | dict] | None = None,
        margins: float | tuple[float, float, float, float] | dict | None = None,
        coordinate_zones: tuple[int, int] | dict | None = None,
        grid: float | dict | None = None,
    ) -> Schematic:
        if not isinstance(name, str):
            raise TypeError("Schematic name must be a string")
        if not name:
            raise ValueError("Schematic name must not be empty")
        metadata = _schematic_sheet_metadata(
            name,
            size=size,
            orientation=orientation,
            title=title,
            number=number,
            page_count=page_count,
            revision=revision,
            date=date,
            project=project,
            file=file,
            title_block=title_block,
            margins=margins,
            coordinate_zones=coordinate_zones,
            grid=grid,
        )
        if name not in self._schematic_sheets or metadata:
            self._schematic_sheets[name] = self._circuit.schematic_sheet(name, metadata)
        return Schematic(self, self._schematic_sheets[name], name)

    def load_schematic_json(self, text: str) -> Schematic:
        if not isinstance(text, str):
            raise TypeError("Schematic JSON must be a string")

        self._circuit.load_schematic_json(text)
        self._schematic_sheets.clear()
        sheet_names = tuple(self._circuit.schematic_sheet_names())
        if not sheet_names:
            raise ValueError("Schematic document must contain at least one sheet")
        return self.schematic(sheet_names[0])

    def load_schematic(self, path: str | Path) -> Schematic:
        return self.load_schematic_json(Path(path).read_text(encoding="utf-8"))

    def validate(self) -> DiagnosticReport:
        return DiagnosticReport(_diagnostic_from_dict(item) for item in self._circuit.validate())

    def validate_for_pcb(self) -> DiagnosticReport:
        return DiagnosticReport(
            _diagnostic_from_dict(item) for item in self._circuit.validate_for_pcb()
        )

    def to_json(self) -> str:
        return self._circuit.to_json()

    def write(self, path: str | Path) -> None:
        Path(path).write_text(self.to_json(), encoding="utf-8")

    def _definition(self, key: str, factory) -> int:
        if key not in self._definitions:
            self._definitions[key] = factory()
        return self._definitions[key]

    def _instantiate(self, key: str, factory, prefix: str, ref: str | None, properties) -> Component:
        definition = self._definition(key, factory)
        if ref is None:
            component = self._circuit.instantiate_auto(definition, prefix, properties)
        else:
            component = self._circuit.instantiate_ref(definition, ref, properties)
        return Component(self, component)


def _number(value: float) -> float:
    if isinstance(value, bool):
        raise TypeError("Electrical attribute values must be numbers")
    if not isinstance(value, (int, float)):
        raise TypeError("Electrical attribute values must be numbers")
    result = float(value)
    if not isfinite(result):
        raise ValueError("Electrical attribute values must be finite")
    return result


def _coordinate(value: float) -> float:
    if isinstance(value, bool):
        raise TypeError("Schematic coordinates must be numbers")
    if not isinstance(value, (int, float)):
        raise TypeError("Schematic coordinates must be numbers")
    result = float(value)
    if not isfinite(result):
        raise ValueError("Schematic coordinates must be finite")
    return result


def _positive_coordinate(value: float, label: str) -> float:
    result = _coordinate(value)
    if result <= 0:
        raise ValueError(f"{label} must be positive")
    return result


def _nonnegative_coordinate(value: float, label: str) -> float:
    result = _coordinate(value)
    if result < 0:
        raise ValueError(f"{label} must not be negative")
    return result


def _string_dict(values: dict, context: str) -> dict[str, str]:
    if not isinstance(values, dict):
        raise TypeError(f"{context} must be a dict")
    result: dict[str, str] = {}
    for key, value in values.items():
        if not isinstance(key, str):
            raise TypeError(f"{context} keys must be strings")
        if not key:
            raise ValueError(f"{context} keys must not be empty")
        if not isinstance(value, str):
            raise TypeError(f"{context} values must be strings")
        if not value:
            raise ValueError(f"{context} values must not be empty")
        result[key] = value
    return result


def _sheet_orientation(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic sheet orientation must be a string")
    normalized = value.casefold()
    if normalized == "portrait":
        return "Portrait"
    if normalized == "landscape":
        return "Landscape"
    raise ValueError("Schematic sheet orientation must be portrait or landscape")


def _schematic_sheet_size(value, orientation: str) -> dict[str, float]:
    if isinstance(value, str):
        if value.casefold() != "a4":
            raise ValueError("Schematic sheet size names must be A4")
        if orientation == "Portrait":
            return {"width": 210.0, "height": 297.0}
        return {"width": 297.0, "height": 210.0}
    if isinstance(value, dict):
        return {
            "width": _positive_coordinate(value["width"], "Schematic sheet widths"),
            "height": _positive_coordinate(value["height"], "Schematic sheet heights"),
        }
    if isinstance(value, (tuple, list)) and len(value) == 2:
        return {
            "width": _positive_coordinate(value[0], "Schematic sheet widths"),
            "height": _positive_coordinate(value[1], "Schematic sheet heights"),
        }
    raise TypeError("Schematic sheet size must be A4, a (width, height) pair, or a dict")


def _title_block_value(value, label: str) -> str:
    if not isinstance(value, (str, int)):
        raise TypeError(f"Schematic title-block {label} must be a string or integer")
    result = str(value)
    if not result:
        raise ValueError(f"Schematic title-block {label} must not be empty")
    return result


def _title_block_items(values) -> list[dict[str, str]]:
    if values is None:
        return []
    if isinstance(values, dict):
        iterable = values.items()
    else:
        iterable = values
    result: list[dict[str, str]] = []
    for item in iterable:
        if isinstance(item, dict):
            key = item["key"]
            value = item["value"]
        else:
            key, value = item
        if not isinstance(key, str):
            raise TypeError("Schematic title-block keys must be strings")
        if not key:
            raise ValueError("Schematic title-block keys must not be empty")
        result.append({"key": key, "value": _title_block_value(value, key)})
    return result


def _sheet_margins(value) -> dict[str, float]:
    if isinstance(value, dict):
        return {
            "left": _coordinate(value["left"]),
            "top": _coordinate(value["top"]),
            "right": _coordinate(value["right"]),
            "bottom": _coordinate(value["bottom"]),
        }
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        margin = _coordinate(value)
        return {"left": margin, "top": margin, "right": margin, "bottom": margin}
    if isinstance(value, (tuple, list)) and len(value) == 4:
        return {
            "left": _coordinate(value[0]),
            "top": _coordinate(value[1]),
            "right": _coordinate(value[2]),
            "bottom": _coordinate(value[3]),
        }
    raise TypeError("Schematic sheet margins must be a number, four-value tuple, or dict")


def _positive_count(value, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError(f"{label} must be an integer")
    if value <= 0:
        raise ValueError(f"{label} must be positive")
    return value


def _visibility_flag(value, label: str) -> bool:
    if not isinstance(value, bool):
        raise TypeError(f"{label} must be a boolean")
    return value


def _coordinate_zones(value) -> dict[str, int | bool]:
    if isinstance(value, dict):
        return {
            "columns": _positive_count(value["columns"], "Coordinate zone columns"),
            "rows": _positive_count(value["rows"], "Coordinate zone rows"),
            "visible": _visibility_flag(
                value.get("visible", True), "Coordinate zone visibility"
            ),
        }
    if isinstance(value, (tuple, list)) and len(value) == 2:
        return {
            "columns": _positive_count(value[0], "Coordinate zone columns"),
            "rows": _positive_count(value[1], "Coordinate zone rows"),
            "visible": True,
        }
    raise TypeError("Coordinate zones must be a (columns, rows) pair or dict")


def _sheet_grid(value) -> dict[str, float | bool]:
    if isinstance(value, dict):
        return {
            "spacing": _positive_coordinate(value["spacing"], "Schematic grid spacing"),
            "visible": _visibility_flag(
                value.get("visible", True), "Schematic grid visibility"
            ),
        }
    return {"spacing": _positive_coordinate(value, "Schematic grid spacing"), "visible": True}


def _schematic_sheet_metadata(
    name: str,
    *,
    size,
    orientation,
    title,
    number,
    page_count,
    revision,
    date,
    project,
    file,
    title_block,
    margins,
    coordinate_zones,
    grid,
) -> dict:
    metadata: dict = {}
    orientation_value: str | None = None
    if orientation is not None:
        orientation_value = _sheet_orientation(orientation)
        metadata["orientation"] = orientation_value
    if size is not None:
        orientation_value = orientation_value or "Landscape"
        metadata["orientation"] = orientation_value
        metadata["size"] = _schematic_sheet_size(size, orientation_value)
    if title is not None:
        if not isinstance(title, str):
            raise TypeError("Schematic sheet titles must be strings")
        if not title:
            raise ValueError("Schematic sheet titles must not be empty")
        metadata["title"] = title

    fields = []
    for key, value in (
        ("Number", number),
        ("Page Count", page_count),
        ("Revision", revision),
        ("Date", date),
        ("Project", project),
        ("File", file),
    ):
        if value is not None:
            fields.append({"key": key, "value": _title_block_value(value, key)})
    fields.extend(_title_block_items(title_block))
    if fields:
        metadata["title_block"] = fields
    if margins is not None:
        metadata["frame"] = {"visible": True, "margins": _sheet_margins(margins)}
    if coordinate_zones is not None:
        metadata["coordinate_zones"] = _coordinate_zones(coordinate_zones)
    if grid is not None:
        metadata["grid"] = _sheet_grid(grid)
    return metadata


def _schematic_svg_page_filename(name: str) -> str:
    safe = "".join(
        character
        if character.isascii() and (character.isalnum() or character in "._-")
        else "_"
        for character in name
    ).strip("._")
    return safe or "sheet"


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
        return _schematic_point(value, design=design)
    except ValueError as error:
        _raise_cross_design_with_context(error, schematic, action)


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


def _pin_anchor_net(anchor: SchematicPinAnchor) -> Net | None:
    net_index = anchor.pin._design._circuit.net_of(anchor.pin.index)
    if net_index is None:
        return None
    return _net_by_index(anchor.pin._design, net_index)


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


def _require_pin_anchor_matches_net(anchor: SchematicPinAnchor, net: Net) -> None:
    pin_net = _pin_anchor_net(anchor)
    pin_label = _pin_anchor_label(anchor)
    if pin_net is None:
        raise ValueError(
            f"Cannot draw {_net_label(net)} at {pin_label}: the pin is not connected "
            "to any logical net"
        )
    if pin_net.index != net.index:
        raise ValueError(
            f"Cannot draw {_net_label(net)} at {pin_label}: the pin belongs to "
            f"{_net_label(pin_net)}"
        )


def _require_port_matches_net(port: SchematicPort, net: Net) -> None:
    if port.net._design is not net._design:
        raise ValueError(_cross_design_anchor_message(port))
    if port.net.index != net.index:
        raise ValueError(
            f"Cannot draw {_net_label(net)} through schematic port {port.name!r}: "
            f"the port belongs to {_net_label(port.net)}"
        )


def _resolve_schematic_port_net(
    design: Design,
    at: tuple[float, float] | SchematicAnchor | SchematicPort,
    net: Net | None,
    *,
    schematic: Schematic,
    action: str,
    type_message: str = "Schematic power ports expect a Net handle",
) -> Net:
    context = _schematic_authoring_context(schematic, action)
    try:
        explicit = _validate_explicit_schematic_net(design, net, type_message=type_message)
    except ValueError as error:
        _raise_cross_design_with_context(error, schematic, action)
    if isinstance(at, SchematicPinAnchor):
        _require_schematic_point_design_for_authoring(
            at, design, schematic=schematic, action=action
        )
        if explicit is None:
            inferred = _pin_anchor_net(at)
            if inferred is None:
                raise ValueError(
                    f"Cannot infer logical net for {context} at {_pin_anchor_label(at)}; "
                    "connect the pin in the logical model first or pass net="
                )
            return inferred
        try:
            _require_pin_anchor_matches_net(at, explicit)
        except ValueError as error:
            raise ValueError(_with_schematic_context(str(error), schematic, action)) from error
        return explicit
    if isinstance(at, SchematicPort):
        if explicit is None:
            if at.net._design is not design:
                raise ValueError(
                    _with_schematic_context(_cross_design_anchor_message(at), schematic, action)
                )
            return at.net
        try:
            _require_port_matches_net(at, explicit)
        except ValueError as error:
            raise ValueError(_with_schematic_context(str(error), schematic, action)) from error
    elif isinstance(at, SchematicAnchor):
        _require_schematic_point_design_for_authoring(
            at, design, schematic=schematic, action=action
        )
    if explicit is None:
        raise ValueError(
            f"Cannot infer logical net for {context} from a non-pin anchor; pass net="
        )
    return explicit


def _resolve_schematic_sheet_port_net(
    design: Design,
    name: str,
    at: tuple[float, float] | SchematicAnchor | SchematicPort,
    net: Net | None,
    *,
    schematic: Schematic,
) -> Net:
    if net is None and not isinstance(at, (SchematicPinAnchor, SchematicPort)):
        return _net_by_name(design, name, context="Schematic sheet ports")
    return _resolve_schematic_port_net(
        design,
        at,
        net,
        schematic=schematic,
        action="sheet port",
        type_message="Schematic sheet ports expect a Net handle",
    )


def _resolve_schematic_connection_net(
    design: Design,
    start: tuple[float, float] | SchematicAnchor | SchematicPort,
    end: tuple[float, float] | SchematicAnchor | SchematicPort,
    net: Net | None,
    *,
    schematic: Schematic,
) -> Net:
    context = _schematic_authoring_context(schematic, "schematic wire")
    try:
        explicit = _validate_explicit_schematic_net(
            design, net, type_message="Schematic connections expect a Net handle"
        )
    except ValueError as error:
        _raise_cross_design_with_context(error, schematic, "schematic wire")
    pin_nets: list[tuple[SchematicPinAnchor, Net | None]] = []
    for endpoint in (start, end):
        if isinstance(endpoint, SchematicPinAnchor):
            _require_schematic_point_design_for_authoring(
                endpoint, design, schematic=schematic, action="schematic wire"
            )
            pin_nets.append((endpoint, _pin_anchor_net(endpoint)))
        elif isinstance(endpoint, SchematicPort):
            if explicit is None:
                if endpoint.net._design is not design:
                    raise ValueError(
                        _with_schematic_context(
                            _cross_design_anchor_message(endpoint),
                            schematic,
                            "schematic wire",
                        )
                    )
            else:
                try:
                    _require_port_matches_net(endpoint, explicit)
                except ValueError as error:
                    raise ValueError(
                        _with_schematic_context(str(error), schematic, "schematic wire")
                    ) from error
        elif isinstance(endpoint, SchematicAnchor):
            _require_schematic_point_design_for_authoring(
                endpoint, design, schematic=schematic, action="schematic wire"
            )

    if explicit is not None:
        for anchor, _pin_net in pin_nets:
            try:
                _require_pin_anchor_matches_net(anchor, explicit)
            except ValueError as error:
                raise ValueError(
                    _with_schematic_context(str(error), schematic, "schematic wire")
                ) from error
        return explicit

    if len(pin_nets) != 2:
        raise ValueError(
            f"Cannot infer schematic wire net on {_schematic_sheet_phrase(schematic)} "
            "unless both endpoints are placed pin anchors on the same logical net; "
            "pass explicit net="
        )

    (first_anchor, first_net), (second_anchor, second_net) = pin_nets
    if first_net is None:
        raise ValueError(
            f"Cannot infer schematic wire net on {_schematic_sheet_phrase(schematic)}: "
            f"{_pin_anchor_label(first_anchor)} is not connected to any logical net"
        )
    if second_net is None:
        raise ValueError(
            f"Cannot infer schematic wire net on {_schematic_sheet_phrase(schematic)}: "
            f"{_pin_anchor_label(second_anchor)} is not connected to any logical net"
        )
    if first_net.index != second_net.index:
        raise ValueError(
            f"Cannot infer schematic wire net on {_schematic_sheet_phrase(schematic)} "
            "because endpoints belong to different logical nets: "
            f"{_pin_anchor_label(first_anchor)} is on {_net_label(first_net)}, but "
            f"{_pin_anchor_label(second_anchor)} is on {_net_label(second_net)}"
        )
    return first_net


def _resolve_schematic_signal_net(
    design: Design,
    name_or_net: str | Net,
    at: tuple[float, float] | SchematicAnchor | SchematicPort,
    *,
    schematic: Schematic,
    action: str,
) -> Net:
    try:
        net = _resolve_schematic_net_label(design, name_or_net)
    except ValueError as error:
        _raise_cross_design_with_context(error, schematic, action)
    if isinstance(at, SchematicPinAnchor):
        _require_schematic_point_design_for_authoring(
            at,
            design,
            schematic=schematic,
            action=action,
        )
        try:
            _require_pin_anchor_matches_net(at, net)
        except ValueError as error:
            raise ValueError(_with_schematic_context(str(error), schematic, action)) from error
    elif isinstance(at, SchematicPort):
        try:
            _require_port_matches_net(at, net)
        except ValueError as error:
            raise ValueError(_with_schematic_context(str(error), schematic, action)) from error
    elif isinstance(at, SchematicAnchor):
        _require_schematic_point_design_for_authoring(
            at,
            design,
            schematic=schematic,
            action=action,
        )
    return net


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


def _symbol_point(value: tuple[float, float]) -> dict:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise TypeError("Schematic symbol points must be (x, y) pairs")
    return {"x": _coordinate(value[0]), "y": _coordinate(value[1])}


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
            SchematicSymbolSpec.line((0, 0), (4, 0)),
            SchematicSymbolSpec.rectangle((4, -3), (16, 3)),
            SchematicSymbolSpec.line((16, 0), (20, 0)),
            SchematicSymbolSpec.text("R", (10, -8)),
        ),
    )


def _capacitor_symbol_spec(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("1", 1, "2", 2),
        primitives=(
            SchematicSymbolSpec.line((0, 0), (8, 0)),
            SchematicSymbolSpec.line((8, -5), (8, 5)),
            SchematicSymbolSpec.line((12, -5), (12, 5)),
            SchematicSymbolSpec.line((12, 0), (20, 0)),
            SchematicSymbolSpec.text("C", (10, -10)),
        ),
    )


def _inductor_symbol_spec(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("1", 1, "2", 2),
        primitives=(
            SchematicSymbolSpec.line((0, 0), (4, 0)),
            SchematicSymbolSpec.arc((6, 0), 2, 180, -180),
            SchematicSymbolSpec.arc((10, 0), 2, 180, -180),
            SchematicSymbolSpec.arc((14, 0), 2, 180, -180),
            SchematicSymbolSpec.line((16, 0), (20, 0)),
            SchematicSymbolSpec.text("L", (10, -8)),
        ),
    )


def _diode_symbol_spec(name: str) -> SchematicSymbolSpec:
    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("K", 1, "A", 2),
        primitives=(
            SchematicSymbolSpec.line((0, 0), (7, 0)),
            SchematicSymbolSpec.line((7, -5), (7, 5)),
            SchematicSymbolSpec.line((7, -5), (13, 0)),
            SchematicSymbolSpec.line((7, 5), (13, 0)),
            SchematicSymbolSpec.line((13, 0), (20, 0)),
            SchematicSymbolSpec.text("D", (10, -11)),
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
        _presentation_symbol_name(symbol.name, length, reverse, flip),
        pins=pins,
        primitives=primitives,
    )


def _presentation_symbol_name(
    base_name: str, length: float, reverse: bool, flip: bool
) -> str:
    length_token = f"{length:g}".replace("-", "m").replace(".", "p")
    flags = []
    if reverse:
        flags.append("reverse")
    if flip:
        flags.append("flip")
    flag_token = "-".join(flags) if flags else "scaled"
    return f"{base_name}#two-terminal-{flag_token}-{length_token}"


def _presentation_primitive(
    symbol: SchematicSymbolSpec,
    primitive: dict,
    length: float,
    reverse: bool,
    flip: bool,
) -> dict:
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


def _presentation_point(
    symbol: SchematicSymbolSpec,
    point: tuple[float, float],
    length: float,
    reverse: bool,
    flip: bool,
) -> tuple[float, float]:
    pins = tuple(symbol.pins)
    start, base_length = _symbol_terminal_frame(symbol)
    end = pins[-1].at
    ux = (end[0] - start[0]) / base_length
    uy = (end[1] - start[1]) / base_length
    vx = -uy
    vy = ux
    dx = point[0] - start[0]
    dy = point[1] - start[1]
    along = dx * ux + dy * uy
    across = dx * vx + dy * vy
    scaled_along = along * length / base_length
    if reverse:
        scaled_along = length - scaled_along
    if flip:
        across = -across
    return (_coordinate(scaled_along), _coordinate(across))


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


def _component_json(component: Component) -> dict:
    logical = json.loads(component._design.to_json())
    return next(
        item for item in logical["components"] if item["id"] == f"component:{component.index}"
    )


def _component_property(component: Component, name: str):
    target = _component_json(component)
    value = target["properties"].get(name)
    if value is None:
        return None
    return value["value"]


def _component_value_label(component: Component) -> str | None:
    target = _component_json(component)
    for name in ("value", "Value"):
        value = target["properties"].get(name)
        if value is not None:
            return str(value["value"])

    attributes = target.get("electrical_attributes", {})
    for name in ("resistance", "capacitance", "inductance", "voltage", "current", "power"):
        if name in attributes:
            formatted = _format_electrical_attribute(name, attributes[name])
            if formatted is not None:
                return formatted
    for name in sorted(attributes):
        formatted = _format_electrical_attribute(name, attributes[name])
        if formatted is not None:
            return formatted
    return None


def _format_electrical_attribute(name: str, attribute: dict) -> str | None:
    if attribute.get("type") != "quantity":
        return None
    value = attribute.get("value")
    # JSON booleans are ints in Python; quantities must render only real numbers.
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return None
    unit = {
        "resistance": "ohm",
        "capacitance": "F",
        "inductance": "H",
        "voltage": "V",
        "current": "A",
        "power": "W",
    }.get(name, attribute.get("dimension", ""))
    number = f"{float(value):g}"
    return f"{number} {unit}" if unit else number


def _label_offset(offset: float | None, ofst: float | None) -> float:
    if offset is not None and ofst is not None:
        raise ValueError("Use either offset= or ofst= for schematic element labels")
    value = 10 if offset is None and ofst is None else offset if ofst is None else ofst
    return _coordinate(value)


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


def _orthogonal_wire_points(
    points: Iterable[tuple[float, float]],
) -> tuple[tuple[float, float], ...]:
    """Return orthogonal route points while preserving explicit author points.

    For a simple two-point diagonal route, insert a single horizontal-then-vertical
    bend. For routes with explicit ``via()`` points, keep the authored path unchanged.
    """
    result = tuple(points)
    if len(result) == 2:
        start, end = result
        if start[0] != end[0] and start[1] != end[1]:
            midpoint = (end[0], start[1])
            return (start, midpoint, end)
    return result


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


def _normalize_schematic_route_points(
    points: Iterable[tuple[float, float]],
) -> tuple[tuple[float, float], ...]:
    result: list[tuple[float, float]] = []
    for point in points:
        converted = (_coordinate(point[0]), _coordinate(point[1]))
        if not result or result[-1] != converted:
            result.append(converted)
    if len(result) < 2:
        raise ValueError("Schematic wire route must contain at least two distinct points")
    return tuple(result)


def _schematic_route_has_distinct_points(points: Iterable[tuple[float, float]]) -> bool:
    first: tuple[float, float] | None = None
    for point in points:
        if first is None:
            first = point
        elif point != first:
            return True
    return False


def _shape_wire_points(
    start: tuple[float, float],
    end: tuple[float, float],
    *,
    shape: str,
    k: float | None,
) -> tuple[tuple[float, float], ...]:
    if not isinstance(shape, str):
        raise TypeError("Schematic wire shape must be a string")
    normalized_shape = {"n": "|-|", "c": "-|-"}.get(shape, shape)
    valid_shapes = ("-", "-|", "|-", "|-|", "-|-")
    if normalized_shape not in valid_shapes:
        raise ValueError(
            "Schematic wire shape must be one of -, -|, |-, |-|, n, -|-, or c"
        )

    sx, sy = start
    ex, ey = end
    if normalized_shape == "-":
        return _normalize_schematic_route_points((start, end))

    offset = None if k is None else _coordinate(k)
    if normalized_shape == "-|":
        bend_x = ex if offset is None else sx + offset
        return _normalize_schematic_route_points(
            (start, (bend_x, sy), (bend_x, ey), end)
        )
    if normalized_shape == "|-":
        bend_y = ey if offset is None else sy + offset
        return _normalize_schematic_route_points(
            (start, (sx, bend_y), (ex, bend_y), end)
        )
    if normalized_shape == "|-|":
        bend_y = (sy + ey) / 2 if offset is None else sy + offset
        return _normalize_schematic_route_points(
            (start, (sx, bend_y), (ex, bend_y), end)
        )

    bend_x = (sx + ex) / 2 if offset is None else sx + offset
    return _normalize_schematic_route_points(
        (start, (bend_x, sy), (bend_x, ey), end)
    )


def _pin_refs_by_name(pin_refs, name: str):
    return tuple(item for item in pin_refs if item["name"] == name)


def _pin_ref_alias(pin_ref) -> str:
    return f"{pin_ref['name']}_{pin_ref['number']}"


def _resolve_single_pin_ref(pin_refs, key: str, *, missing_message: str, ambiguous_message: str):
    matches = _pin_refs_by_name(pin_refs, key)
    if len(matches) == 1:
        return matches[0]["index"]
    if len(matches) > 1:
        aliases = ", ".join(repr(_pin_ref_alias(item)) for item in matches)
        raise ValueError(f"{ambiguous_message}; explicit aliases: {aliases}")

    alias_matches = tuple(item for item in pin_refs if _pin_ref_alias(item) == key)
    if len(alias_matches) == 1:
        return alias_matches[0]["index"]
    if len(alias_matches) > 1:
        raise ValueError(f"Pin alias {key!r} is ambiguous")

    raise IndexError(missing_message)


def _flatten_pins(values):
    for value in values:
        if isinstance(value, Pin):
            yield value
        elif isinstance(value, PinGroup):
            yield from value
        elif isinstance(value, (tuple, list)):
            yield from _flatten_pins(value)
        else:
            yield value


def _flatten_module_endpoints(values):
    for value in values:
        if isinstance(value, (ModuleNet, ModulePin)):
            yield value
        elif isinstance(value, ModulePinGroup):
            yield from value
        elif isinstance(value, (tuple, list)):
            yield from _flatten_module_endpoints(value)
        else:
            yield value


def _connect_module_endpoints(endpoints) -> ModuleNet:
    endpoints = tuple(endpoints)
    nets = [endpoint for endpoint in endpoints if isinstance(endpoint, ModuleNet)]
    if len(nets) != 1:
        raise TypeError("Module connections need exactly one ModuleNet or ModulePort")

    net = nets[0]
    for endpoint in endpoints:
        if endpoint is net:
            continue
        if not isinstance(endpoint, ModulePin):
            raise TypeError("Module nets can only connect ModulePin handles")
        if endpoint._component._module is not net._module:
            raise ValueError("Module pin belongs to a different module")
        net._module._design._circuit.connect_module_pin(
            net._module.index, net.index, endpoint._component.index, endpoint.index
        )
    return net


def _diagnostic_from_dict(item) -> Diagnostic:
    return Diagnostic(
        severity=item["severity"],
        code=item["code"],
        message=item["message"],
        entities=tuple(
            DiagnosticEntity(entity["kind"], entity["index"]) for entity in item["entities"]
        ),
    )


__all__ = [
    "Component",
    "ComponentDefinition",
    "Design",
    "Diagnostic",
    "DiagnosticEntity",
    "DiagnosticReport",
    "Library",
    "LibraryComponent",
    "ModuleComponent",
    "ModuleComponentInfo",
    "ModuleComponentOriginInfo",
    "ModuleConnectionInfo",
    "ModuleDefinition",
    "ModuleInstance",
    "ModuleInstancePort",
    "ModuleNetOriginInfo",
    "ModuleNet",
    "ModulePinGroup",
    "ModulePin",
    "ModulePort",
    "Net",
    "Pin",
    "PinGroup",
    "PinSpec",
    "PhysicalPartSpec",
    "PlacedSchematicElement",
    "PortBindingInfo",
    "PortInfo",
    "Schematic",
    "SchematicAnchor",
    "SchematicBlockPinSpec",
    "SchematicDrawing",
    "SchematicJunction",
    "SchematicNetLabel",
    "SchematicNoConnect",
    "SchematicPinAnchor",
    "SchematicPort",
    "SchematicRegion",
    "SchematicSignalStub",
    "SchematicSymbolField",
    "SchematicSymbolPinSpec",
    "SchematicSymbolSpec",
    "SchematicSymbol",
    "SchematicTwoTerminalElement",
    "SchematicWire",
    "SchematicWireBuilder",
    "TemplateNetInfo",
]
