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
                missing_message="Component has no pin with that name",
                ambiguous_message=(
                    f"Component pin name {key!r} is ambiguous; use pins({key!r}) "
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
    ):
        self._schematic = schematic
        self._index = index
        self._component = component
        self._orientation = orientation

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
        return self._pin_anchor_for_ref(_resolve_schematic_symbol_pin_ref(self._pin_refs(), key))

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

    def _pin_anchor_for_ref(self, pin_ref) -> SchematicPinAnchor:
        if self._component is None:
            raise ValueError(
                "Schematic pin anchors require the Component handle returned by Schematic.place()"
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
                f"Placed schematic element pin name {name!r} is ambiguous; use bracket "
                f"access by pin number or pin(number). Matching pin numbers: {numbers}"
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
        offset: float = 10,
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
        at = _element_label_point(self, loc, offset)
        self.symbol._schematic._add_symbol_field(
            self.symbol,
            name=name,
            value=text,
            at=at,
            orient=self.orientation if orient is None else orient,
        )
        return self

    def label_value(
        self,
        *,
        loc: str = "top",
        offset: float = 10,
        orient: str | None = None,
    ) -> PlacedSchematicElement:
        value = _component_property(self.component, "value")
        if value is None:
            raise ValueError("Component has no value property to label")
        return self.label(str(value), loc=loc, name="value", offset=offset, orient=orient)

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

    def __init__(self, schematic: Schematic, net: Net, *, drawing=None):
        self._schematic = schematic
        self._net = net
        self._points: list[tuple[float, float]] = []
        self._drawing = drawing
        self._start_here = drawing.here if drawing is not None else None
        self._start_direction = drawing.direction if drawing is not None else None
        self._wire: SchematicWire | None = None

    def at(self, point) -> SchematicWireBuilder:
        return self.from_(point)

    def from_(self, point) -> SchematicWireBuilder:
        self._require_unmaterialized()
        self._points = [_schematic_point(point, design=self._schematic._design)]
        self._update_drawing_cursor(self._points[-1])
        return self

    def via(self, point) -> SchematicWireBuilder:
        """Append an explicit intermediate point that the route should preserve."""
        self._require_unmaterialized()
        self._require_started()
        self._append_point(_schematic_point(point, design=self._schematic._design))
        return self

    def to(self, point, *, shape: str | None = None, k: float | None = None):
        """Append the next route point, normally the terminal endpoint."""
        self._require_unmaterialized()
        self._require_started()
        self._append_point(_schematic_point(point, design=self._schematic._design))
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
                _schematic_axis_target(anchor_or_x, self._schematic._design, "x"),
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
                _schematic_axis_target(anchor_or_y, self._schematic._design, "y"),
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
            raise ValueError("Schematic wire shape routes need exactly two endpoints")
        route_intent = "Direct" if shape == "-" else "Orthogonal"
        return self._persist(
            _shape_wire_points(self._points[0], self._points[1], shape=shape, k=k),
            route_intent=route_intent,
            normalize=True,
        )

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
    ):
        self._schematic = schematic
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
            raise ValueError("Schematic drawing state stack is empty")
        self._here, self._direction = self._stack.pop()
        return self

    def two_terminal(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
    ) -> SchematicTwoTerminalElement:
        self._flush_pending()
        element = SchematicTwoTerminalElement(
            self,
            component,
            symbol=symbol,
            variant=variant,
        )
        self._pending = element
        return element

    def R(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(component, symbol=symbol, variant=variant)

    def C(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(component, symbol=symbol, variant=variant)

    def L(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(component, symbol=symbol, variant=variant)

    def D(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(component, symbol=symbol, variant=variant)

    def LED(
        self,
        component: Component,
        *,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
    ) -> SchematicTwoTerminalElement:
        return self.two_terminal(component, symbol=symbol, variant=variant)

    def place(
        self,
        component: Component,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort | None = None,
        orient: str | None = None,
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
    ) -> PlacedSchematicElement:
        self._flush_pending()
        placed = self._schematic.place(
            component,
            at=self._here if at is None else at,
            orient=self._direction if orient is None else orient,
            symbol=symbol,
            variant=variant,
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
        return self._schematic.connect(start, end, net=net, shape=shape, k=k)

    def wire(self, net: Net) -> SchematicWireBuilder:
        self._flush_pending()
        builder = self._schematic.wire(net)
        builder._drawing = self
        builder._start_here = self._here
        builder._start_direction = self._direction
        builder.from_(self._here)
        self._pending = builder
        return builder

    def line(self, net: Net) -> SchematicWireBuilder:
        return self.wire(net)

    def power(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Up",
    ) -> SchematicPort:
        self._flush_pending()
        return self._schematic.power(name, at=at, net=net, orient=orient)

    def ground(
        self,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Down",
    ) -> SchematicPort:
        self._flush_pending()
        return self._schematic.ground(at=at, net=net, orient=orient)

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

    def __enter__(self) -> SchematicDrawing:
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        if exc_type is None:
            self._flush_pending()
        return False

    def _anchor_at(
        self, value: tuple[float, float] | SchematicAnchor | SchematicPort
    ) -> SchematicAnchor:
        return SchematicAnchor(
            _schematic_point(value, design=self._schematic._design),
            design=self._schematic._design,
        )

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
    ):
        if not isinstance(component, Component):
            raise TypeError("Two-terminal placement expects a Component handle")
        if component._design is not drawing._schematic._design:
            raise ValueError("Component belongs to a different design")
        pins = tuple(component._pin_refs())
        if len(pins) != 2:
            raise ValueError("Two-terminal placement requires exactly two component pins")
        self._drawing = drawing
        self._component = component
        self._symbol = symbol
        self._variant = variant
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
        offset: float = 10,
        orient: str | None = None,
    ) -> PlacedSchematicElement:
        return self._materialize().label(
            text, loc=loc, name=name, offset=offset, orient=orient
        )

    def label_value(
        self,
        *,
        loc: str = "top",
        offset: float = 10,
        orient: str | None = None,
    ) -> PlacedSchematicElement:
        return self._materialize().label_value(loc=loc, offset=offset, orient=orient)

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
            raise ValueError(f"Two-terminal anchor {ref!r} is ambiguous")
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
                raise ValueError(f"No schematic symbol found for variant {self._variant!r}")
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

    def place(
        self,
        component: Component,
        *,
        at: tuple[float, float],
        orient: str = "Right",
        symbol: str | SchematicSymbolSpec | None = None,
        variant: str = "default",
    ) -> SchematicSymbol:
        if not isinstance(component, Component):
            raise TypeError("Schematic placement expects a Component handle")
        if component._design is not self._design:
            raise ValueError("Component belongs to a different design")
        if symbol is None:
            symbol = component.schematic_symbol_variant(variant)
            if symbol is None:
                raise ValueError(f"No schematic symbol found for variant {variant!r}")
        if isinstance(symbol, SchematicSymbolSpec):
            self.register_symbol(symbol)
            symbol_name = symbol.name
        elif isinstance(symbol, str):
            symbol_name = symbol
        else:
            raise TypeError("symbol must be a string or SchematicSymbolSpec")
        if not symbol_name:
            raise ValueError("symbol must not be empty")
        orientation = _orientation(orient)
        x, y = _schematic_point(at, design=self._design)
        instance = self._design._circuit.place_schematic_symbol(
            self._sheet_index, component.index, symbol_name, x, y, orientation
        )
        return SchematicSymbol(self, instance, component, orientation)

    def register_symbol(self, symbol: SchematicSymbolSpec) -> None:
        if not isinstance(symbol, SchematicSymbolSpec):
            raise TypeError("register_symbol expects a SchematicSymbolSpec")
        self._design._register_schematic_symbol(symbol)

    def wire(
        self,
        net: Net,
        points: Iterable[tuple[float, float] | SchematicAnchor | SchematicPort] | None = None,
    ) -> SchematicWire | SchematicWireBuilder:
        if not isinstance(net, Net):
            raise TypeError("Schematic wires expect a Net handle")
        if net._design is not self._design:
            raise ValueError("Net belongs to a different design")
        if points is None:
            return SchematicWireBuilder(self, net)

        return self._add_wire(net, points, route_intent="Direct")

    def connect(
        self,
        start: tuple[float, float] | SchematicAnchor | SchematicPort,
        end: tuple[float, float] | SchematicAnchor | SchematicPort,
        *,
        net: Net | None = None,
        shape: str | None = None,
        k: float | None = None,
    ) -> SchematicWire:
        resolved_net = _resolve_schematic_connection_net(self._design, start, end, net)
        builder = self.wire(resolved_net).from_(start).to(end)
        if shape is None:
            return builder.orthogonal()
        return builder.shape(shape, k=k)

    def _add_wire(
        self,
        net: Net,
        points: Iterable[tuple[float, float]],
        *,
        route_intent: str,
    ) -> SchematicWire:
        wire_points = tuple(points)
        if len(wire_points) < 2:
            raise ValueError("Schematic wires need at least two points")

        converted = []
        for point in wire_points:
            converted.append(_schematic_point(point, design=self._design))

        wire = self._design._circuit.add_schematic_wire(
            self._sheet_index, net.index, converted, route_intent
        )
        return SchematicWire(self, wire)

    def label(
        self,
        net: Net,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
    ) -> SchematicNetLabel:
        if not isinstance(net, Net):
            raise TypeError("Schematic labels expect a Net handle")
        if net._design is not self._design:
            raise ValueError("Net belongs to a different design")
        x, y = _schematic_point(at, design=self._design)
        orientation = _orientation(orient)

        label = self._design._circuit.add_schematic_net_label(
            self._sheet_index, net.index, x, y, orientation
        )
        return SchematicNetLabel(self, label, orientation)

    def _add_symbol_field(
        self,
        symbol: SchematicSymbol,
        *,
        name: str,
        value: str,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
    ) -> SchematicSymbolField:
        if not isinstance(symbol, SchematicSymbol):
            raise TypeError("Schematic symbol fields expect a placed symbol handle")
        if symbol._schematic is not self:
            raise ValueError("Schematic symbol belongs to a different schematic")
        x, y = _schematic_point(at, design=self._design)
        orientation = _orientation(orient)

        field = self._design._circuit.add_schematic_symbol_field(
            self._sheet_index, symbol.index, name, value, x, y, orientation
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
        self, net: Net, *, at: tuple[float, float] | SchematicAnchor | SchematicPort
    ) -> SchematicJunction:
        if not isinstance(net, Net):
            raise TypeError("Schematic junctions expect a Net handle")
        if net._design is not self._design:
            raise ValueError("Net belongs to a different design")
        x, y = _schematic_point(at, design=self._design)
        junction = self._design._circuit.add_schematic_junction(
            self._sheet_index, net.index, x, y
        )
        return SchematicJunction(self, junction)

    def power(
        self,
        name: str,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Up",
    ) -> SchematicPort:
        net = _resolve_schematic_port_net(self._design, at, net, context="power port")
        return self._power_port(name, net=net, at=at, orient=orient, kind="Power")

    def ground(
        self,
        *,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        net: Net | None = None,
        orient: str = "Down",
    ) -> SchematicPort:
        net = _resolve_schematic_port_net(self._design, at, net, context="ground port")
        return self._power_port(net.name, net=net, at=at, orient=orient, kind="Ground")

    def _power_port(
        self,
        name: str,
        *,
        net: Net,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str,
        kind: str,
    ) -> SchematicPort:
        if not isinstance(name, str):
            raise TypeError("Schematic power port names must be strings")
        if not name:
            raise ValueError("Schematic power port names must not be empty")
        if not isinstance(net, Net):
            raise TypeError("Schematic power ports expect a Net handle")
        if net._design is not self._design:
            raise ValueError("Net belongs to a different design")
        if name != net.name:
            raise ValueError("Schematic power port names must match the logical net name")
        x, y = _schematic_point(at, design=self._design)
        orientation = _orientation(orient)
        port = self._design._circuit.add_schematic_power_port(
            self._sheet_index, net.index, kind, x, y, orientation
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
    ) -> SchematicNoConnect:
        if not isinstance(pin, SchematicPinAnchor):
            raise TypeError("Schematic no-connect markers expect a placed pin anchor")
        if reason is not None and not isinstance(reason, str):
            raise TypeError("Schematic no-connect reasons must be strings")
        _require_schematic_point_design(pin, self._design)
        orientation = _orientation(orient)
        marker = self._design._circuit.add_schematic_no_connect_marker(
            self._sheet_index, pin.pin.index, pin.x, pin.y, orientation, reason or ""
        )
        return SchematicNoConnect(self, marker, pin.pin)

    def sheet_port(
        self,
        name: str,
        *,
        net: Net,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        kind: str = "Bidirectional",
        orient: str = "Right",
    ) -> SchematicPort:
        if not isinstance(name, str):
            raise TypeError("Schematic sheet port names must be strings")
        if not name:
            raise ValueError("Schematic sheet port names must not be empty")
        if not isinstance(net, Net):
            raise TypeError("Schematic sheet ports expect a Net handle")
        if net._design is not self._design:
            raise ValueError("Net belongs to a different design")
        x, y = _schematic_point(at, design=self._design)
        orientation = _orientation(orient)
        port_kind = _sheet_port_kind(kind)
        port = self._design._circuit.add_schematic_sheet_port(
            self._sheet_index, net.index, name, port_kind, x, y, orientation
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
        net: Net,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Right",
    ) -> SchematicPort:
        return self.sheet_port(name, net=net, at=at, kind="OffPage", orient=orient)

    def to_json(self) -> str:
        return self._design._circuit.schematic_to_json()

    def to_svg(self) -> str:
        return self._design._circuit.schematic_to_svg()

    def validate(self) -> DiagnosticReport:
        return DiagnosticReport(
            _diagnostic_from_dict(item)
            for item in self._design._circuit.validate_schematic()
        )

    def write_svg(self, path: str | Path) -> None:
        Path(path).write_text(self.to_svg(), encoding="utf-8")

    def write_json(self, path: str | Path) -> None:
        Path(path).write_text(self.to_json(), encoding="utf-8")

    def __repr__(self) -> str:
        return f"Schematic(name={self.name!r}, sheet_index={self._sheet_index})"


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

    def schematic(self, name: str) -> Schematic:
        if not isinstance(name, str):
            raise TypeError("Schematic name must be a string")
        if not name:
            raise ValueError("Schematic name must not be empty")
        if name not in self._schematic_sheets:
            self._schematic_sheets[name] = self._circuit.schematic_sheet(name)
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


def _schematic_point_tuple(value) -> tuple[float, float]:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise TypeError("Schematic points must be anchors, ports, or (x, y) pairs")
    return (_coordinate(value[0]), _coordinate(value[1]))


def _require_schematic_point_design(value, design: Design) -> None:
    point_design = getattr(value, "_design", None)
    if point_design is not None and point_design is not design:
        raise ValueError("Schematic anchor belongs to a different design")


def _schematic_point(value, *, design: Design) -> tuple[float, float]:
    if isinstance(value, SchematicPort):
        if value.net._design is not design:
            raise ValueError("Schematic anchor belongs to a different design")
        return value.pin.point
    if isinstance(value, SchematicAnchor):
        _require_schematic_point_design(value, design)
        return value.point
    return _schematic_point_tuple(value)


def _require_schematic_net(net: Net, design: Design, *, type_message: str) -> None:
    if not isinstance(net, Net):
        raise TypeError(type_message)
    if net._design is not design:
        raise ValueError("Net belongs to a different design")


def _net_by_index(design: Design, index: int) -> Net:
    for net in design.nets():
        if net.index == index:
            return net
    raise ValueError(f"Kernel returned missing logical net net:{index}")


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
        raise ValueError("Schematic anchor belongs to a different design")
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
    context: str,
) -> Net:
    explicit = _validate_explicit_schematic_net(
        design,
        net,
        type_message="Schematic power ports expect a Net handle",
    )
    if isinstance(at, SchematicPinAnchor):
        _require_schematic_point_design(at, design)
        if explicit is None:
            inferred = _pin_anchor_net(at)
            if inferred is None:
                raise ValueError(
                    f"Cannot infer logical net for {context} at {_pin_anchor_label(at)}; "
                    "connect the pin in the logical model first or pass net="
                )
            return inferred
        _require_pin_anchor_matches_net(at, explicit)
        return explicit
    if isinstance(at, SchematicPort) and explicit is not None:
        _require_port_matches_net(at, explicit)
    if explicit is None:
        raise ValueError(
            f"Cannot infer logical net for {context} from a non-pin anchor; pass net="
        )
    return explicit


def _resolve_schematic_connection_net(
    design: Design,
    start: tuple[float, float] | SchematicAnchor | SchematicPort,
    end: tuple[float, float] | SchematicAnchor | SchematicPort,
    net: Net | None,
) -> Net:
    explicit = _validate_explicit_schematic_net(
        design,
        net,
        type_message="Schematic connections expect a Net handle",
    )
    pin_nets: list[tuple[SchematicPinAnchor, Net | None]] = []
    for endpoint in (start, end):
        if isinstance(endpoint, SchematicPinAnchor):
            _require_schematic_point_design(endpoint, design)
            pin_nets.append((endpoint, _pin_anchor_net(endpoint)))
        elif isinstance(endpoint, SchematicPort) and explicit is not None:
            _require_port_matches_net(endpoint, explicit)

    if explicit is not None:
        for anchor, _pin_net in pin_nets:
            _require_pin_anchor_matches_net(anchor, explicit)
        return explicit

    if len(pin_nets) != 2:
        raise ValueError(
            "Cannot infer schematic wire net unless both endpoints are placed pin "
            "anchors on the same logical net; pass explicit net="
        )

    (first_anchor, first_net), (second_anchor, second_net) = pin_nets
    if first_net is None:
        raise ValueError(
            f"Cannot infer schematic wire net: {_pin_anchor_label(first_anchor)} is not "
            "connected to any logical net"
        )
    if second_net is None:
        raise ValueError(
            f"Cannot infer schematic wire net: {_pin_anchor_label(second_anchor)} is not "
            "connected to any logical net"
        )
    if first_net.index != second_net.index:
        raise ValueError(
            "Cannot infer schematic wire net because endpoints belong to different "
            f"logical nets: {_pin_anchor_label(first_anchor)} is on {_net_label(first_net)}, "
            f"but {_pin_anchor_label(second_anchor)} is on {_net_label(second_net)}"
        )
    return first_net


def _symbol_point(value: tuple[float, float]) -> dict:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise TypeError("Schematic symbol points must be (x, y) pairs")
    return {"x": _coordinate(value[0]), "y": _coordinate(value[1])}


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


def _component_property(component: Component, name: str):
    logical = json.loads(component._design.to_json())
    target = next(
        item for item in logical["components"] if item["id"] == f"component:{component.index}"
    )
    value = target["properties"].get(name)
    if value is None:
        return None
    return value["value"]


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
    if normalized == "top":
        return center.up(distance)
    if normalized == "bottom":
        return center.down(distance)
    if normalized == "left":
        return center.left(distance)
    return center.right(distance)


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
        raise ValueError(
            f"Schematic symbol pin name {key!r} is ambiguous; use pins({key!r}) "
            "for the group or address one physical pin by number"
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


def _schematic_axis_target(value, design: Design, axis: str) -> float:
    if isinstance(value, bool):
        raise TypeError("Schematic coordinates must be numbers")
    if isinstance(value, (int, float)):
        return _coordinate(value)
    point = _schematic_point(value, design=design)
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
    "SchematicDrawing",
    "SchematicJunction",
    "SchematicNetLabel",
    "SchematicNoConnect",
    "SchematicPinAnchor",
    "SchematicPort",
    "SchematicSymbolField",
    "SchematicSymbolPinSpec",
    "SchematicSymbolSpec",
    "SchematicSymbol",
    "SchematicTwoTerminalElement",
    "SchematicWire",
    "SchematicWireBuilder",
    "TemplateNetInfo",
]
