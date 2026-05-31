"""Design root for the Volt Python authoring facade."""

from __future__ import annotations

from pathlib import Path
from typing import Iterable

from . import _volt
from ._footprint import Footprint, FootprintRef
from ._utils import _number
from .diagnostics import DiagnosticReport, _diagnostic_from_dict
from .library import (
    LibraryComponent,
    PinSpec,
    SchematicSymbolSpec,
    _normalize_schematic_symbols,
    _schematic_symbol_refs,
)
from .logical import Component, ComponentDefinition, ModuleDefinition, ModuleInstance, Net
from ._schematic_metadata import _schematic_sheet_metadata
from .schematic import Schematic


class Design:
    """Root Python handle for one kernel-owned logical circuit."""

    def __init__(self, name: str):
        if not name:
            raise ValueError("Design name must not be empty")

        self.name = name
        self._circuit = _volt.Circuit()
        self._definitions: dict[str, int] = {}
        self._library_definitions: dict[tuple[str, str, str], ComponentDefinition] = {}
        self._object_footprints: dict[FootprintRef, Footprint] = {}
        self._component_object_footprints: dict[int, FootprintRef] = {}
        self._board_cached_footprints: dict[FootprintRef, tuple[int, Footprint]] = {}
        self._board_placed_components: list[int] = []
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

    def _object_footprint_for_component(self, component: int) -> Footprint | None:
        ref = self._component_object_footprints.get(component)
        if ref is None:
            return None
        return self._object_footprints[ref]

    def _record_board_placement(self, component: int) -> None:
        self._board_placed_components.append(component)

    def _ensure_board_footprint_cached(self, footprint: Footprint) -> int:
        cached = self._board_cached_footprints.get(footprint.ref)
        if cached is not None:
            footprint_id, cached_footprint = cached
            if cached_footprint._to_dict() != footprint._to_dict():
                library, name = footprint.ref
                raise RuntimeError(
                    f"Board footprint definition {library}:{name} conflicts with cached geometry"
                )
            return footprint_id

        footprint_id = self._circuit.board_cache_footprint_definition(footprint._to_dict())
        self._board_cached_footprints[footprint.ref] = (footprint_id, footprint)
        return footprint_id

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

    def board(self, name: str = "Main"):
        from .pcb import Board

        return Board(self, name)

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
