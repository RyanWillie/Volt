"""Python authoring surface for Volt logical circuits."""

from __future__ import annotations

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


class SchematicWireBuilder:
    """Fluent authoring helper for one schematic wire run.

    Start with ``from_()``, append any explicit intermediate anchors with ``via()``,
    append the intended endpoint with ``to()``, then persist the run with ``direct()``
    or ``orthogonal()``.
    """

    def __init__(self, schematic: Schematic, net: Net):
        self._schematic = schematic
        self._net = net
        self._points: list[tuple[float, float]] = []

    def from_(self, point) -> SchematicWireBuilder:
        self._points = [_schematic_point(point, design=self._schematic._design)]
        return self

    def via(self, point) -> SchematicWireBuilder:
        """Append an explicit intermediate point that the route should preserve."""
        self._require_started()
        self._points.append(_schematic_point(point, design=self._schematic._design))
        return self

    def to(self, point) -> SchematicWireBuilder:
        """Append the next route point, normally the terminal endpoint."""
        self._require_started()
        self._points.append(_schematic_point(point, design=self._schematic._design))
        return self

    def direct(self) -> SchematicWire:
        """Persist the collected points without inserting an automatic bend."""
        return self._schematic._add_wire(self._net, self._points, route_intent="Direct")

    def orthogonal(self) -> SchematicWire:
        """Persist the run, inserting one bend only for a two-point diagonal route."""
        return self._schematic._add_wire(
            self._net, _orthogonal_wire_points(self._points), route_intent="Orthogonal"
        )

    def _require_started(self) -> None:
        if not self._points:
            raise ValueError("Schematic wire builder must start with from_()")


class Schematic:
    """Handle to kernel-owned schematic projection data for one sheet."""

    def __init__(self, design: Design, sheet_index: int, name: str):
        self._design = design
        self._sheet_index = sheet_index
        self.name = name

    @property
    def sheet_index(self) -> int:
        return self._sheet_index

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
        net: Net,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Up",
    ) -> SchematicPort:
        return self._power_port(name, net=net, at=at, orient=orient, kind="Power")

    def ground(
        self,
        *,
        net: Net,
        at: tuple[float, float] | SchematicAnchor | SchematicPort,
        orient: str = "Down",
    ) -> SchematicPort:
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

    def connector_1x02(self, *, ref: str | None = None) -> Component:
        return self._instantiate(
            "connector_1x02", self._circuit.define_connector_1x02, "J", ref, {}
        )

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
    "PortBindingInfo",
    "PortInfo",
    "Schematic",
    "SchematicAnchor",
    "SchematicJunction",
    "SchematicNetLabel",
    "SchematicNoConnect",
    "SchematicPinAnchor",
    "SchematicPort",
    "SchematicSymbolPinSpec",
    "SchematicSymbolSpec",
    "SchematicSymbol",
    "SchematicWire",
    "SchematicWireBuilder",
    "TemplateNetInfo",
]
