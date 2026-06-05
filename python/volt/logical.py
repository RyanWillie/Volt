"""Logical circuit handles for the Volt Python facade."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Iterator

from ._footprint import Footprint, FootprintInput, footprint_ref
from ._utils import _number
from .library import LibraryComponent, PinPadValue


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


class ComponentDefinition:
    """Handle to a kernel-owned reusable component definition."""

    def __init__(self, design: Design, index: int, name: str):
        self._design = design
        self._index = index
        self.name = name

    @property
    def index(self) -> int:
        """Return the kernel index for this component definition."""
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
        """Return the kernel index for this module definition."""
        return self._index

    def net(self, name: str, *, kind: str = "signal") -> ModuleNet:
        """Create a template-local net inside this module definition."""
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
        """Create a module boundary port backed by an internal template net."""
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
        """Instantiate a component template inside this module definition."""
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
        """Connect module-local pins to exactly one module net or port."""
        return _connect_module_endpoints(_flatten_module_endpoints(endpoints))

    def template_nets(self) -> tuple[TemplateNetInfo, ...]:
        """Return read-only descriptions of this module's template nets."""
        return tuple(
            TemplateNetInfo(item["index"], item["name"], item["kind"])
            for item in self._design._circuit.template_nets(self._index)
        )

    def ports(self) -> tuple[PortInfo, ...]:
        """Return read-only descriptions of this module's boundary ports."""
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
        """Return read-only descriptions of component templates in this module."""
        return tuple(
            ModuleComponentInfo(item["index"], item["definition"], item["reference"])
            for item in self._design._circuit.module_components(self._index)
        )

    def connections(self) -> tuple[ModuleConnectionInfo, ...]:
        """Return read-only descriptions of module-local pin connections."""
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
        """Return the kernel index for this module template net."""
        return self._index

    def connect(self, *pins: ModulePin | Iterable[ModulePin]) -> ModuleNet:
        """Connect module-local pins to this template net."""
        return _connect_module_endpoints((self, *_flatten_module_endpoints(pins)))

    def __iadd__(self, pins: ModulePin | Iterable[ModulePin]) -> ModuleNet:
        """Connect module-local pins to this template net with ``+=`` syntax."""
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
        """Return the kernel index for this module boundary port."""
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
        """Return the kernel index for this module-local pin definition."""
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
        """Iterate over the module pins in this repeated-name group."""
        return iter(self._pins)

    def __len__(self) -> int:
        """Return the number of module pins in this group."""
        return len(self._pins)

    def __getitem__(self, index: int) -> ModulePin:
        """Return one module pin from this group by positional index."""
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
        """Return the kernel index for this module component template."""
        return self._index

    def __getitem__(self, key: int | str) -> ModulePin:
        """Return a module component pin by physical number, name, or explicit alias."""
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
        """Return every module component pin that has the given name."""
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
        """Return the kernel index for the module port definition."""
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
        """Return the kernel index for this root module instance."""
        return self._index

    def __getitem__(self, key: str) -> ModuleInstancePort:
        """Return a root module instance port by port name."""
        if not isinstance(key, str):
            raise TypeError("Module instance ports are addressed by str name")
        if key not in self._ports_by_name:
            raise KeyError(key)
        return ModuleInstancePort(self, self._ports_by_name[key], key)

    def component(self, ref: str) -> Component:
        """Return the concrete component created from a module component reference."""
        if ref not in self._components_by_ref:
            raise KeyError(ref)
        return Component(self._design, self._components_by_ref[ref])

    def net_origins(self) -> tuple[ModuleNetOriginInfo, ...]:
        """Return concrete nets mapped back to their module template nets."""
        return tuple(
            ModuleNetOriginInfo(item["template_net"], item["net"])
            for item in self._design._circuit.module_net_origins(self._index)
        )

    def component_origins(self) -> tuple[ModuleComponentOriginInfo, ...]:
        """Return concrete components mapped back to their module templates."""
        return tuple(
            ModuleComponentOriginInfo(item["module_component"], item["component"])
            for item in self._design._circuit.module_component_origins(self._index)
        )

    def port_bindings(self) -> tuple[PortBindingInfo, ...]:
        """Return parent-net bindings for this module instance's ports."""
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
        """Return the kernel index for this concrete pin."""
        return self._index

    @property
    def component(self) -> Component:
        """Return the component that owns this concrete pin."""
        return Component(self._design, self._design._circuit.pin_component(self._index))

    @property
    def component_reference(self) -> str:
        """Return the reference designator for the component that owns this pin."""
        return self.component.reference

    @property
    def name(self) -> str:
        """Return the logical pin name from its reusable pin definition."""
        return self._pin_ref()["name"]

    @property
    def number(self) -> str:
        """Return the logical pin number from its reusable pin definition."""
        return self._pin_ref()["number"]

    def mark_no_connect(self) -> Pin:
        """Mark this pin as intentionally unconnected for diagnostics."""
        self._design._circuit.mark_intentional_no_connect_pin(self._index)
        return self

    def _pin_ref(self):
        component = self._design._circuit.pin_component(self._index)
        for item in self._design._circuit.pin_refs(component):
            if item["index"] == self._index:
                return item
        raise RuntimeError("Pin handle does not belong to its reported component")

    def __repr__(self) -> str:
        return f"Pin(index={self._index})"


class PinGroup:
    """Repeated-label concrete pins selected as a group."""

    def __init__(self, component: Component, name: str, pins: Iterable[Pin]):
        self._component = component
        self.name = name
        self._pins = tuple(pins)

    def __iter__(self) -> Iterator[Pin]:
        """Iterate over the concrete pins in this repeated-name group."""
        return iter(self._pins)

    def __len__(self) -> int:
        """Return the number of concrete pins in this group."""
        return len(self._pins)

    def __getitem__(self, index: int) -> Pin:
        """Return one concrete pin from this group by positional index."""
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
        """Return the kernel index for this component instance."""
        return self._index

    @property
    def reference(self) -> str:
        """Return the schematic reference designator for this component."""
        return self._design._circuit.component_reference(self._index)

    @property
    def schematic_symbol(self) -> SchematicSymbolSpec | str | None:
        """Return the default schematic symbol for this component, if one is registered."""
        return self.schematic_symbol_variant("default")

    def schematic_symbol_variant(self, variant: str) -> SchematicSymbolSpec | str | None:
        """Return the schematic symbol registered for a named symbol variant."""
        if not isinstance(variant, str):
            raise TypeError("schematic symbol variant must be a string")
        if not variant:
            raise ValueError("schematic symbol variant must not be empty")
        name = self._design._circuit.component_schematic_symbol(self._index, variant)
        if name is None:
            return None
        return self._design._schematic_symbols.get(name, name)

    def __getitem__(self, key: int | str) -> Pin:
        """Return a component pin by physical number, name, or explicit alias."""
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
        """Return every component pin that has the given name."""
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
        footprint: FootprintInput,
        pin_pads: dict[int | str, PinPadValue],
        properties: dict | None = None,
        voltage_rating: float | None = None,
        power_rating: float | None = None,
    ) -> Component:
        """Attach selected physical part data; pass a Footprint object for board-ready geometry."""
        if not isinstance(pin_pads, dict):
            raise TypeError("pin_pads must be a dict")
        object_footprint = footprint if isinstance(footprint, Footprint) else None
        if object_footprint is not None:
            self._design._check_object_footprint(object_footprint)
        footprint_library, footprint_name = footprint_ref(footprint)

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
            footprint_library,
            footprint_name,
            pin_pads,
            properties or {},
        )
        for name, dimension, value in selected_part_ratings:
            self._design._circuit.set_selected_part_quantity(self._index, name, dimension, value)
        if object_footprint is not None:
            self._design._register_component_object_footprint(self._index, object_footprint)
        else:
            self._design._clear_component_object_footprint(self._index)
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
        """Return the kernel index for this logical net."""
        return self._index

    def mark_stub(self) -> Net:
        """Mark this net as intentionally exposed by a schematic stub."""
        self._design._circuit.mark_intentional_stub_net(self._index)
        return self

    def pins(self) -> tuple[Pin, ...]:
        """Return the concrete pins currently connected to this net."""
        return tuple(
            Pin(self._design, index) for index in self._design._circuit.net_pins(self._index)
        )

    def connect(self, *pins: Pin | ModuleInstancePort | Iterable[Pin | ModuleInstancePort]) -> Net:
        """Connect concrete pins or module instance ports to this logical net."""
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
        """Connect pins or module instance ports to this net with ``+=`` syntax."""
        return self.connect(pins)

    def __repr__(self) -> str:
        return f"Net(name={self.name!r}, index={self._index})"



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
