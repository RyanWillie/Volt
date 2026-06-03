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
    """Normalized footprint pad geometry for board-ready footprints."""

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
        """Create a surface-mount footprint pad definition."""
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
        """Create a through-hole footprint pad definition with drill metadata."""
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


@dataclass(frozen=True)
class KiCadLossWarning:
    """Structured warning for an unsupported or lossy KiCad adapter construct."""

    kind: str
    construct: str
    message: str
    severity: str


@dataclass(frozen=True)
class KiCadPcbExport:
    """Result of exporting a board projection to a KiCad PCB file."""

    text: str
    warnings: tuple[KiCadLossWarning, ...]


@dataclass(frozen=True)
class Hole:
    """Generic circular board hole primitive."""

    center: Point
    diameter: float
    plated: bool = False
    role: str = ""
    label: str = ""
    finished_diameter: float | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "center", _point(self.center, "Board hole center"))


@dataclass(frozen=True)
class MountingHole:
    """Generic mounting hole primitive."""

    center: Point
    diameter: float
    label: str = ""
    plated: bool = False
    finished_diameter: float | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "center", _point(self.center, "Board mounting hole center"))


@dataclass(frozen=True)
class ToolingHole:
    """Generic tooling hole primitive."""

    center: Point
    diameter: float
    label: str = ""
    finished_diameter: float | None = None

    def __post_init__(self) -> None:
        object.__setattr__(self, "center", _point(self.center, "Board tooling hole center"))


@dataclass(frozen=True)
class Slot:
    """Generic slotted board hole primitive."""

    start: Point
    end: Point
    width: float
    plated: bool = False
    role: str = ""
    label: str = ""

    def __post_init__(self) -> None:
        object.__setattr__(self, "start", _point(self.start, "Board slot start"))
        object.__setattr__(self, "end", _point(self.end, "Board slot end"))


@dataclass(frozen=True)
class Cutout:
    """Generic board cutout primitive."""

    outline: tuple[Point, ...]
    role: str = ""
    label: str = ""

    @classmethod
    def polygon(
        cls, vertices: Iterable[Point], *, role: str = "", label: str = ""
    ) -> "Cutout":
        """Create a polygonal board cutout."""
        return cls(
            tuple(_point(vertex, "Board cutout vertex") for vertex in vertices),
            role,
            label,
        )


@dataclass(frozen=True)
class Fiducial:
    """Generic board fiducial primitive."""

    center: Point
    diameter: float
    label: str = ""
    side: str = "top"

    def __post_init__(self) -> None:
        object.__setattr__(self, "center", _point(self.center, "Board fiducial center"))


@dataclass(frozen=True)
class Text:
    """Generic board text/annotation primitive."""

    text: str
    at: Point
    layer: int
    rotation: float = 0.0
    size: float = 1.0
    locked: bool = False

    def __post_init__(self) -> None:
        object.__setattr__(self, "at", _point(self.at, "Board text position"))


@dataclass(frozen=True)
class MechanicalKeepout:
    """Generic mechanical keepout primitive."""

    outline: tuple[Point, ...]
    layers: tuple[int, ...]
    restrictions: tuple[str, ...] = ("all",)

    def __init__(
        self,
        *,
        outline: Iterable[Point],
        layers: Iterable[int],
        restrictions: Iterable[str] = ("all",),
    ):
        object.__setattr__(
            self,
            "outline",
            tuple(_point(point, "Board keepout outline point") for point in outline),
        )
        object.__setattr__(self, "layers", tuple(_layer_indices(layers)))
        object.__setattr__(self, "restrictions", tuple(restrictions))


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
        """Return the board design-rule values currently stored in the kernel."""
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
        """Update board design rules, preserving unspecified rule values."""
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
        """Add a physical or logical board layer and return its kernel index."""
        return self._design._circuit.board_add_layer(name, role, side, float(thickness), enabled)

    def set_layer_stack(self, layers: Iterable[int], *, thickness: float) -> Board:
        """Set the board layer stack order and total thickness."""
        self._design._circuit.board_set_layer_stack(
            [_layer_index(layer) for layer in layers], float(thickness)
        )
        return self

    def set_rectangular_outline(self, *, origin: Point, size: Point) -> Board:
        """Set a rectangular board outline from an origin and size."""
        x, y = _point(origin, "Board outline origin")
        width, height = _point(size, "Board outline size")
        self._design._circuit.board_set_rectangular_outline(x, y, width, height)
        return self

    def set_polygon_outline(self, vertices: Iterable[Point]) -> Board:
        """Set a polygon board outline from ordered vertices."""
        self._design._circuit.board_set_polygon_outline(
            [_point(vertex, "Board outline vertex") for vertex in vertices]
        )
        return self

    def add(self, primitive) -> int:
        """Add a generic board primitive and return its kernel index."""
        if isinstance(primitive, Hole):
            x, y = primitive.center
            return self._design._circuit.board_add_hole(
                primitive.label,
                x,
                y,
                float(primitive.diameter),
                primitive.plated,
                primitive.role,
                None if primitive.finished_diameter is None else float(primitive.finished_diameter),
            )
        if isinstance(primitive, MountingHole):
            x, y = primitive.center
            return self._design._circuit.board_add_mounting_hole(
                primitive.label,
                x,
                y,
                float(primitive.diameter),
                primitive.plated,
                None if primitive.finished_diameter is None else float(primitive.finished_diameter),
            )
        if isinstance(primitive, ToolingHole):
            x, y = primitive.center
            return self._design._circuit.board_add_tooling_hole(
                primitive.label,
                x,
                y,
                float(primitive.diameter),
                None if primitive.finished_diameter is None else float(primitive.finished_diameter),
            )
        if isinstance(primitive, Slot):
            start_x, start_y = primitive.start
            end_x, end_y = primitive.end
            return self._design._circuit.board_add_slot(
                primitive.label,
                start_x,
                start_y,
                end_x,
                end_y,
                float(primitive.width),
                primitive.plated,
                primitive.role,
            )
        if isinstance(primitive, Cutout):
            return self._design._circuit.board_add_cutout(
                primitive.label, list(primitive.outline), primitive.role
            )
        if isinstance(primitive, Fiducial):
            x, y = primitive.center
            return self._design._circuit.board_add_fiducial(
                primitive.label, x, y, float(primitive.diameter), primitive.side
            )
        if isinstance(primitive, Text):
            x, y = primitive.at
            return self._design._circuit.board_add_text(
                primitive.text,
                x,
                y,
                _layer_index(primitive.layer),
                float(primitive.rotation),
                float(primitive.size),
                primitive.locked,
            )
        if isinstance(primitive, MechanicalKeepout):
            return self._design._circuit.board_add_keepout(
                list(primitive.layers),
                list(primitive.outline),
                list(primitive.restrictions),
            )
        raise TypeError("Board.add expects a Volt board primitive")

    def add_mounting_hole(self, label: str, *, at: Point, diameter: float) -> int:
        """Add a board-level mounting hole and return its kernel index."""
        x, y = _point(at, "Board mounting hole position")
        return self._design._circuit.board_add_mounting_hole(
            label, x, y, float(diameter), False, None
        )

    def cache_footprint(self, footprint: Footprint) -> int:
        """Cache an explicit board-owned footprint definition for importers and low-level tests."""
        if not isinstance(footprint, Footprint):
            raise TypeError("cache_footprint expects a Footprint")
        return self._design._ensure_board_footprint_cached(footprint)

    def place(
        self,
        component: Component | int,
        *,
        at: Point,
        rotation: float = 0.0,
        side: str = "top",
        locked: bool = False,
    ) -> int:
        """Place a component footprint on the board and return the placement index."""
        if isinstance(component, Component):
            if component._design is not self._design:
                raise ValueError("Component belongs to a different design")
            component_index = component.index
        else:
            component_index = _component_index(component)
        x, y = _point(at, "Board placement position")
        self._sync_component_object_footprint(component_index)
        placement = self._design._circuit.board_place_component(
            component_index, x, y, float(rotation), side, locked
        )
        self._design._record_board_placement(component_index)
        return placement

    def add_track(
        self,
        net: Net | int,
        *,
        layer: int,
        points: Iterable[Point],
        width: float,
    ) -> int:
        """Add a routed track segment sequence for a logical net."""
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
        """Add a via connecting a net between two board layers."""
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
        """Add a copper zone, optionally bound to a logical net."""
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
        """Add a keepout region with layer and feature restrictions."""
        return self.add(
            MechanicalKeepout(outline=outline, layers=layers, restrictions=restrictions)
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
        """Add a board text item and return its kernel index."""
        return self.add(
            Text(text, at=at, layer=layer, rotation=rotation, size=size, locked=locked)
        )

    def resolve_pads(self) -> tuple[PadResolution, ...]:
        """Resolve placed footprint pads to component pins and logical nets."""
        self._sync_object_footprints()
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
        """Run PCB projection validation and return the diagnostic report."""
        self._sync_object_footprints()
        return DiagnosticReport(
            _diagnostic_from_dict(item) for item in self._design._circuit.board_validate()
        )

    def to_json(self) -> str:
        """Serialize the PCB projection to Volt board JSON."""
        self._sync_object_footprints()
        return self._design._circuit.board_to_json()

    def to_svg(self, *, pad_net_overlays: bool = True, diagnostic_overlays: bool = True) -> str:
        """Render the PCB projection as SVG."""
        self._sync_object_footprints()
        return self._design._circuit.board_to_svg(pad_net_overlays, diagnostic_overlays)

    def to_kicad_pcb(self) -> KiCadPcbExport:
        """Export the PCB projection to a KiCad `.kicad_pcb` adapter document."""
        self._sync_object_footprints()
        result = self._design._circuit.board_to_kicad_pcb()
        return KiCadPcbExport(
            text=result["text"],
            warnings=tuple(KiCadLossWarning(**warning) for warning in result["warnings"]),
        )

    def _sync_component_object_footprint(self, component: int) -> None:
        footprint = self._design._object_footprint_for_component(component)
        if footprint is not None:
            self._design._ensure_board_footprint_cached(footprint)

    def _sync_object_footprints(self) -> None:
        for component in self._design._board_placed_components:
            self._sync_component_object_footprint(component)

    def write_json(self, path: str | Path) -> None:
        """Write the PCB projection JSON to a file."""
        Path(path).write_text(self.to_json(), encoding="utf-8")

    def write_svg(self, path: str | Path) -> None:
        """Write the PCB projection SVG to a file."""
        Path(path).write_text(self.to_svg(), encoding="utf-8")

    def write_kicad_pcb(self, path: str | Path) -> KiCadPcbExport:
        """Write the KiCad PCB adapter document and return its loss report."""
        export = self.to_kicad_pcb()
        Path(path).write_text(export.text, encoding="utf-8")
        return export
