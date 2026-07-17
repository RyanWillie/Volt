"""Design root for the Volt Python authoring facade."""

from __future__ import annotations
import json
from pathlib import Path
from typing import Iterable, Mapping

from . import _volt
from ._footprint import Footprint, FootprintRef
from ._utils import _number
from .diagnostics import DiagnosticReport, _diagnostic_from_dict
from .library import (
    LibraryComponent,
    PartModel3D,
    PinSpec,
    SchematicSymbolSpec,
    _SelectedPartModel3D,
    _normalize_schematic_symbols,
    _schematic_symbol_refs,
)
from .logical import Component, ComponentDefinition, ModuleDefinition, ModuleInstance, Net, NetClass
from .part import Part, _PartDefinition
from ._schematic_metadata import _schematic_sheet_metadata
from .schematic import Schematic


class Design:
    """Root Python handle for one kernel-owned logical circuit."""

    def __init__(self, name: str):
        if not name:
            raise ValueError("Design name must not be empty")

        self.name = name
        self._circuit = _volt.Circuit()
        self._schematic_document = _volt.SchematicDocument(self._circuit)
        self._board_registry = _volt.BoardRegistry(self._circuit)
        self._boards: dict[str, object] = {}
        self._owner = self._circuit
        self._definitions: dict[str, int] = {}
        self._library_definitions: dict[tuple[str, str, str], ComponentDefinition] = {}
        self._object_footprints: dict[FootprintRef, Footprint] = {}
        self._component_object_footprints: dict[int, FootprintRef] = {}
        self._component_model_3d_asset_sources: dict[int, Path] = {}
        self._net_class_counter = 0
        self._schematic_symbols: dict[str, SchematicSymbolSpec] = {}
        self._schematic_sheets: dict[str, int] = {}
        self._sourcing_snapshot: dict[str, dict[str, object]] = {}

    def net(self, name: str, *, kind: str = "signal", voltage: float | None = None) -> Net:
        """Create or return a logical net by name, optionally annotating its voltage."""
        net = Net(self, self._circuit.add_net(name, kind), name)
        if voltage is not None:
            self._circuit.set_net_quantity(net.index, "voltage", "voltage", _number(voltage))
        return net

    def nets(self) -> tuple[Net, ...]:
        """Return handles for every logical net in this design."""
        return tuple(Net(self, item["index"], item["name"]) for item in self._circuit.net_refs())

    def net_class(
        self,
        name: str | None = None,
        *,
        current: float | None = None,
        temp_rise: float = 10.0,
        copper_weight: float = 1.0,
        environment: str = "external",
        track_width: float | None = None,
        via_drill: float | None = None,
        via_diameter: float | None = None,
        clearance: float | None = None,
        voltage: float | None = None,
        dielectric_height: float | None = None,
        spacing_rule: str = "microstrip_2h",
        default_for: str | None = None,
        layer_scope: str = "any_copper",
        priority: int = 0,
    ) -> NetClass:
        """Create a kernel-owned net class, deriving IPC rule values when requested."""
        if name is None:
            name = f"net_class_{self._net_class_counter}"
            self._net_class_counter += 1
        options = {
            "current": None if current is None else _number(current),
            "temp_rise": _number(temp_rise),
            "copper_weight": _number(copper_weight),
            "environment": environment,
            "track_width": None if track_width is None else _number(track_width),
            "via_drill": None if via_drill is None else _number(via_drill),
            "via_diameter": None if via_diameter is None else _number(via_diameter),
            "clearance": None if clearance is None else _number(clearance),
            "voltage": None if voltage is None else _number(voltage),
            "dielectric_height": None if dielectric_height is None else _number(dielectric_height),
            "spacing_rule": spacing_rule,
            "default_for": default_for,
            "layer_scope": layer_scope,
            "priority": int(priority),
        }
        net_class = self._circuit.add_net_class(name, options)
        return NetClass(self, net_class, name)

    def components(self) -> tuple[Component, ...]:
        """Return handles for every concrete component in this design."""
        return tuple(Component(self, item["index"]) for item in self._circuit.component_refs())

    def component(self, reference: str) -> Component:
        """Return a concrete component handle by reference designator."""
        if not isinstance(reference, str):
            raise TypeError("Component references must be strings")
        for item in self._circuit.component_refs():
            if item["reference"] == reference:
                return Component(self, item["index"])
        raise KeyError(reference)

    def R(
        self,
        value: str | None = None,
        *,
        resistance: float | None = None,
        tolerance: float | None = None,
        ref: str | None = None,
    ) -> Component:
        """Instantiate a generic resistor component."""
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
        """Instantiate a generic non-polarized capacitor component."""
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
        """Instantiate a generic polarized capacitor component."""
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
        """Instantiate a generic inductor component."""
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
        """Define a reusable component type with pins, properties, and optional symbols."""
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
        """Define a reusable module template inside this design."""
        module = self._circuit.define_module(name)
        return ModuleDefinition(self, module, name)

    def instantiate(
        self,
        definition: ComponentDefinition | ModuleDefinition | LibraryComponent | Part,
        *,
        ref: str | None = None,
        prefix: str | None = None,
        properties: dict | None = None,
    ) -> Component | ModuleInstance:
        """Instantiate a component definition, module definition, library component, or part."""
        if isinstance(definition, Part):
            definition = definition._to_part_definition()
        elif isinstance(definition, LibraryComponent):
            definition = definition._to_part_definition()

        if isinstance(definition, _PartDefinition):
            component_definition = self._define_part_definition(definition)
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
                    model_3d=part.model_3d,
                    approved_alternate_mpns=part.approved_alternate_mpns,
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
            raise TypeError(
                "instantiate expects a ComponentDefinition, ModuleDefinition, "
                "LibraryComponent, or Part handle"
            )
        if definition._design is not self:
            raise ValueError("Component definition belongs to a different design")
        if prefix is None:
            prefix = "U"
        if ref is None:
            component = self._circuit.instantiate_auto(definition.index, prefix, properties or {})
        else:
            component = self._circuit.instantiate_ref(definition.index, ref, properties or {})
        return Component(self, component)

    def _define_part_definition(self, part: _PartDefinition) -> ComponentDefinition:
        if part.cache_key not in self._library_definitions:
            self._library_definitions[part.cache_key] = self.define_component(
                part.name,
                pins=part.pins,
                properties=part.properties,
                source=(
                    part.source_namespace,
                    part.source_name,
                    part.source_version,
                ),
                schematic_symbol=part.schematic_symbols,
            )
        return self._library_definitions[part.cache_key]

    def _define_library_component(self, component: LibraryComponent) -> ComponentDefinition:
        return self._define_part_definition(component._to_part_definition())

    def _register_schematic_symbol(self, symbol: SchematicSymbolSpec) -> None:
        self._schematic_document.register_schematic_symbol(symbol._to_dict())
        self._schematic_symbols[symbol.name] = symbol

    def _check_object_footprint(self, footprint: Footprint) -> None:
        existing = self._object_footprints.get(footprint.ref)
        if existing is not None and existing._to_dict() != footprint._to_dict():
            library, name = footprint.ref
            raise ValueError(
                f"Footprint {library}:{name} conflicts with already registered geometry"
            )

    def _register_component_object_footprint(self, component: int, footprint: Footprint) -> None:
        self._check_object_footprint(footprint)
        self._object_footprints[footprint.ref] = footprint
        self._component_object_footprints[component] = footprint.ref

    def _clear_component_object_footprint(self, component: int) -> None:
        self._component_object_footprints.pop(component, None)

    def _register_component_model_3d_asset_source(
        self,
        component: int,
        model_3d: PartModel3D,
    ) -> None:
        self._component_model_3d_asset_sources[component] = model_3d.source_path

    def _clear_component_model_3d_asset_source(self, component: int) -> None:
        self._component_model_3d_asset_sources.pop(component, None)

    def _component_model_3d_asset_source(self, component: int) -> Path | None:
        return self._component_model_3d_asset_sources.get(component)

    def _selected_part_model_3d(self, component: int) -> _SelectedPartModel3D | None:
        payload = self._circuit.component_selected_part_model_3d(component)
        if payload is None:
            return None
        return _SelectedPartModel3D.from_payload(payload)

    def _component_reference(self, component: int) -> str:
        return self._circuit.component_reference(component)

    def _object_footprint_for_component(self, component: int) -> Footprint | None:
        ref = self._component_object_footprints.get(component)
        if ref is None:
            return None
        return self._object_footprints[ref]

    def LED(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic LED component."""
        return self._instantiate("led", self._circuit.define_led, "D", ref, {})

    def diode(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic diode component."""
        return self._instantiate("diode", self._circuit.define_diode, "D", ref, {})

    def switch(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic SPST switch component."""
        return self._instantiate("switch_spst", self._circuit.define_switch_spst, "SW", ref, {})

    def crystal(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic two-pin crystal component."""
        return self._instantiate("crystal_2pin", self._circuit.define_crystal_2pin, "Y", ref, {})

    def test_point(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic test point component."""
        return self._instantiate("test_point", self._circuit.define_test_point, "TP", ref, {})

    def connector_1x01(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic one-pin connector component."""
        return self._instantiate(
            "connector_1x01", self._circuit.define_connector_1x01, "J", ref, {}
        )

    def connector_1x02(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic two-pin connector component."""
        return self._instantiate(
            "connector_1x02", self._circuit.define_connector_1x02, "J", ref, {}
        )

    def connector_1x03(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic three-pin connector component."""
        return self._instantiate(
            "connector_1x03", self._circuit.define_connector_1x03, "J", ref, {}
        )

    def regulator(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic three-pin regulator component."""
        return self._instantiate(
            "regulator_3pin", self._circuit.define_regulator_3pin, "U", ref, {}
        )

    def op_amp(self, *, ref: str | None = None) -> Component:
        """Instantiate a generic five-pin operational amplifier component."""
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
        """Create or return a schematic sheet authoring handle by sheet name."""
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
            self._schematic_sheets[name] = self._schematic_document.schematic_sheet(
                name, metadata
            )
        return Schematic(self, self._schematic_sheets[name], name)

    def add_board(self, name: str):
        """Create a complete named PCB alternative, rejecting duplicate names."""
        from .pcb import Board

        native = self._board_registry.add(name)
        board = Board(self, native)
        self._boards[name] = board
        return board

    def board(self, name: str | None = None):
        """Return a Board by exact name, or the only Board in this Design."""
        native = self._board_registry.board(name)
        return self._boards[native.name]

    def boards(self):
        """Return Boards in ascending unsigned UTF-8 BoardName order."""
        return tuple(self._boards[name] for name in self._board_registry.names())

    def load_schematic_json(self, text: str) -> Schematic:
        """Load schematic projection JSON into this design and return the first sheet."""
        if not isinstance(text, str):
            raise TypeError("Schematic JSON must be a string")

        self._schematic_document.load_schematic_json(text)
        self._schematic_sheets.clear()
        sheet_names = tuple(self._schematic_document.schematic_sheet_names())
        if not sheet_names:
            raise ValueError("Schematic document must contain at least one sheet")
        return self.schematic(sheet_names[0])

    def load_schematic(self, path: str | Path) -> Schematic:
        """Load schematic projection JSON from a file and return the first sheet."""
        return self.load_schematic_json(Path(path).read_text(encoding="utf-8"))

    def validate(self) -> DiagnosticReport:
        """Run logical design validation and return the diagnostic report."""
        return DiagnosticReport(_diagnostic_from_dict(item) for item in self._circuit.validate())

    def validate_for_pcb(self) -> DiagnosticReport:
        """Run PCB-readiness validation and return the diagnostic report."""
        return DiagnosticReport(
            _diagnostic_from_dict(item) for item in self._circuit.validate_for_pcb()
        )

    def validate_bom_readiness(self) -> DiagnosticReport:
        """Run BOM-readiness validation and return the diagnostic report."""
        return DiagnosticReport(
            _diagnostic_from_dict(item) for item in self._circuit.validate_bom_readiness()
        )

    def set_sourcing_snapshot(
        self, snapshot: Mapping[str, Mapping[str, object]]
    ) -> "Design":
        """Set design-owned sourcing metadata keyed by manufacturer part number."""
        normalized: dict[str, dict[str, object]] = {}
        for mpn, fields in snapshot.items():
            if not isinstance(mpn, str) or not mpn:
                raise ValueError("Sourcing snapshot MPN keys must be non-empty strings")
            if not isinstance(fields, Mapping):
                raise TypeError("Sourcing snapshot entries must be mappings")
            normalized[mpn] = dict(fields)
        self._sourcing_snapshot = normalized
        return self

    def bom(self) -> dict:
        """Return the deterministic kernel BOM projection as a JSON-compatible dict."""
        return json.loads(self._circuit.bom_json(self._sourcing_snapshot))

    def bom_csv(self) -> str:
        """Return the deterministic kernel BOM projection as CSV text."""
        return self._circuit.bom_csv(self._sourcing_snapshot)

    def _has_sourcing_snapshot(self) -> bool:
        return bool(self._sourcing_snapshot)

    def _bom_sourcing_snapshot_json(self) -> str:
        return self._circuit.bom_sourcing_snapshot_json(self._sourcing_snapshot)

    def to_json(self) -> str:
        """Serialize the logical design to Volt JSON."""
        return self._circuit.to_json()

    def write(self, path: str | Path) -> None:
        """Write the logical design JSON to a file."""
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
