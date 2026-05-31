"""PCB authoring facade over kernel-owned board projections."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from ._footprint import Footprint
from .diagnostics import DiagnosticReport, _diagnostic_from_dict
from .logical import Component, Net


Point = tuple[float, float]


def _point(value, context: str) -> Point:
    if not isinstance(value, tuple) or len(value) != 2:
        raise TypeError(f"{context} must be an (x, y) tuple")
    return (float(value[0]), float(value[1]))


def _layer_index(value: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError("Board layer IDs must be integers")
    return value


def _component_index(value: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError("Board component IDs must be integers")
    return value


def _net_index(value: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError("Board net IDs must be integers")
    return value


def _layer_indices(values: Iterable[int]) -> list[int]:
    return [_layer_index(value) for value in values]


@dataclass(frozen=True)
class FootprintDrill:
    """Drill metadata for a through-hole footprint pad."""

    diameter: float
    plating: str = "plated"

    def _to_dict(self) -> dict:
        return {"diameter": float(self.diameter), "plating": self.plating}


@dataclass(frozen=True)
class FootprintPad:
    """Normalized footprint pad geometry for board projection caches."""

    label: str
    kind: str
    shape: str
    position: Point
    size: Point
    layers: str
    drill: FootprintDrill | None = None
    mechanical_role: str | None = None

    @classmethod
    def surface_mount(
        cls,
        label: str,
        *,
        at: Point,
        size: Point,
        shape: str = "rounded_rectangle",
        layers: str = "front_smd",
        mechanical_role: str | None = None,
    ) -> FootprintPad:
        return cls(
            label,
            "surface_mount",
            shape,
            _point(at, "Footprint pad position"),
            _point(size, "Footprint pad size"),
            layers,
            mechanical_role=mechanical_role,
        )

    @classmethod
    def through_hole(
        cls,
        label: str,
        *,
        at: Point,
        size: Point,
        drill: FootprintDrill,
        shape: str = "circle",
        layers: str = "through_hole",
        mechanical_role: str | None = None,
    ) -> FootprintPad:
        return cls(
            label,
            "through_hole",
            shape,
            _point(at, "Footprint pad position"),
            _point(size, "Footprint pad size"),
            layers,
            drill=drill,
            mechanical_role=mechanical_role,
        )

    def _to_dict(self) -> dict:
        return {
            "label": self.label,
            "kind": self.kind,
            "shape": self.shape,
            "position": self.position,
            "size": self.size,
            "layers": self.layers,
            "drill": None if self.drill is None else self.drill._to_dict(),
            "mechanical_role": self.mechanical_role,
        }


FootprintDefinition = Footprint


@dataclass(frozen=True)
class PadResolution:
    """Derived board-space resolution for one placed footprint pad."""

    placement: int
    component: int
    pad: int
    pad_label: str
    position: Point
    pin: int | None
    net: int | None
    status: str


class Board:
    """Python handle to one kernel-owned PCB projection."""

    def __init__(self, design, name: str = "Main"):
        if not isinstance(name, str):
            raise TypeError("Board name must be a string")
        self._design = design
        info = self._design._circuit.board(name)
        self.name = info["name"]
        self.units = info["units"]

    def design_rules(self) -> dict[str, float]:
        return dict(self._design._circuit.board_design_rules())

    def set_design_rules(
        self,
        *,
        copper_clearance: float | None = None,
        min_track_width: float | None = None,
        min_via_drill: float | None = None,
        min_via_annular: float | None = None,
        board_outline_clearance: float | None = None,
    ) -> Board:
        rules = self.design_rules()
        copper_clearance_value = (
            rules["copper_clearance_mm"] if copper_clearance is None else copper_clearance
        )
        min_track_width_value = (
            rules["minimum_track_width_mm"] if min_track_width is None else min_track_width
        )
        min_via_drill_value = (
            rules["minimum_via_drill_diameter_mm"]
            if min_via_drill is None
            else min_via_drill
        )
        min_via_annular_value = (
            rules["minimum_via_annular_diameter_mm"]
            if min_via_annular is None
            else min_via_annular
        )
        board_outline_clearance_value = (
            rules["board_outline_clearance_mm"]
            if board_outline_clearance is None
            else board_outline_clearance
        )
        self._design._circuit.board_set_design_rules(
            float(copper_clearance_value),
            float(min_track_width_value),
            float(min_via_drill_value),
            float(min_via_annular_value),
            float(board_outline_clearance_value),
        )
        return self

    def add_layer(
        self,
        name: str,
        *,
        role: str,
        side: str,
        thickness: float = 0.0,
        enabled: bool = True,
    ) -> int:
        return self._design._circuit.board_add_layer(name, role, side, float(thickness), enabled)

    def set_layer_stack(self, layers: Iterable[int], *, thickness: float) -> Board:
        self._design._circuit.board_set_layer_stack(
            [_layer_index(layer) for layer in layers], float(thickness)
        )
        return self

    def set_rectangular_outline(self, *, origin: Point, size: Point) -> Board:
        x, y = _point(origin, "Board outline origin")
        width, height = _point(size, "Board outline size")
        self._design._circuit.board_set_rectangular_outline(x, y, width, height)
        return self

    def set_polygon_outline(self, vertices: Iterable[Point]) -> Board:
        self._design._circuit.board_set_polygon_outline(
            [_point(vertex, "Board outline vertex") for vertex in vertices]
        )
        return self

    def add_mounting_hole(self, label: str, *, at: Point, diameter: float) -> int:
        x, y = _point(at, "Board feature position")
        return self._design._circuit.board_add_mounting_hole(label, x, y, float(diameter))

    def cache_footprint(self, footprint: Footprint) -> int:
        if not isinstance(footprint, Footprint):
            raise TypeError("cache_footprint expects a Footprint")
        return self._design._circuit.board_cache_footprint_definition(footprint._to_dict())

    def place(
        self,
        component: Component | int,
        *,
        at: Point,
        rotation: float = 0.0,
        side: str = "top",
        locked: bool = False,
    ) -> int:
        if isinstance(component, Component):
            if component._design is not self._design:
                raise ValueError("Component belongs to a different design")
            component_index = component.index
        else:
            component_index = _component_index(component)
        x, y = _point(at, "Board placement position")
        return self._design._circuit.board_place_component(
            component_index, x, y, float(rotation), side, locked
        )

    def add_track(
        self,
        net: Net | int,
        *,
        layer: int,
        points: Iterable[Point],
        width: float,
    ) -> int:
        if isinstance(net, Net):
            if net._design is not self._design:
                raise ValueError("Net belongs to a different design")
            net_index = net.index
        else:
            net_index = _net_index(net)
        return self._design._circuit.board_add_track(
            net_index,
            _layer_index(layer),
            [_point(point, "Board track point") for point in points],
            float(width),
        )

    def add_via(
        self,
        net: Net | int,
        *,
        at: Point,
        start_layer: int,
        end_layer: int,
        drill: float = 0.30,
        annular: float = 0.70,
    ) -> int:
        if isinstance(net, Net):
            if net._design is not self._design:
                raise ValueError("Net belongs to a different design")
            net_index = net.index
        else:
            net_index = _net_index(net)
        x, y = _point(at, "Board via position")
        return self._design._circuit.board_add_via(
            net_index,
            x,
            y,
            _layer_index(start_layer),
            _layer_index(end_layer),
            float(drill),
            float(annular),
        )

    def add_zone(
        self,
        *,
        outline: Iterable[Point],
        layers: Iterable[int],
        net: Net | int | None = None,
        fill: str = "solid",
        priority: int = 0,
    ) -> int:
        if isinstance(net, Net):
            if net._design is not self._design:
                raise ValueError("Net belongs to a different design")
            net_index = net.index
        elif net is None:
            net_index = None
        else:
            net_index = _net_index(net)
        return self._design._circuit.board_add_zone(
            net_index,
            _layer_indices(layers),
            [_point(point, "Board zone outline point") for point in outline],
            fill,
            int(priority),
        )

    def add_keepout(
        self,
        *,
        outline: Iterable[Point],
        layers: Iterable[int],
        restrictions: Iterable[str],
    ) -> int:
        return self._design._circuit.board_add_keepout(
            _layer_indices(layers),
            [_point(point, "Board keepout outline point") for point in outline],
            list(restrictions),
        )

    def add_text(
        self,
        text: str,
        *,
        at: Point,
        layer: int,
        rotation: float = 0.0,
        size: float = 1.0,
        locked: bool = False,
    ) -> int:
        x, y = _point(at, "Board text position")
        return self._design._circuit.board_add_text(
            text,
            x,
            y,
            _layer_index(layer),
            float(rotation),
            float(size),
            locked,
        )

    def resolve_pads(self) -> tuple[PadResolution, ...]:
        return tuple(
            PadResolution(
                placement=item["placement"],
                component=item["component"],
                pad=item["pad"],
                pad_label=item["pad_label"],
                position=item["position"],
                pin=item["pin"],
                net=item["net"],
                status=item["status"],
            )
            for item in self._design._circuit.board_resolve_pads()
        )

    def validate(self) -> DiagnosticReport:
        return DiagnosticReport(
            _diagnostic_from_dict(item) for item in self._design._circuit.board_validate()
        )

    def to_json(self) -> str:
        return self._design._circuit.board_to_json()

    def to_svg(self, *, pad_net_overlays: bool = True, diagnostic_overlays: bool = True) -> str:
        return self._design._circuit.board_to_svg(pad_net_overlays, diagnostic_overlays)

    def write_json(self, path: str | Path) -> None:
        Path(path).write_text(self.to_json(), encoding="utf-8")

    def write_svg(self, path: str | Path) -> None:
        Path(path).write_text(self.to_svg(), encoding="utf-8")
