"""PCB authoring facade over kernel-owned board projections."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from . import _volt
from ._footprint import Footprint
from .diagnostics import DiagnosticReport, _diagnostic_from_dict
from .library import _SelectedPartModel3D
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


def _capability_clearance_payload(value: Any) -> dict[str, Any]:
    if isinstance(value, dict):
        return dict(value)
    if not isinstance(value, (tuple, list)) or len(value) != 3:
        raise ValueError("Capability profile clearances must be (first, second, clearance)")
    first, second, clearance = value
    return {"first": first, "second": second, "clearance_mm": clearance}


def _capability_refinement_payload(value: Any) -> dict[str, Any]:
    if isinstance(value, dict):
        return dict(value)
    if not isinstance(value, (tuple, list)) or len(value) != 3:
        raise ValueError(
            "Capability profile refinements must be (copper_weight, track_width, clearance)"
        )
    copper_weight, track_width, clearance = value
    return {
        "copper_weight_oz": copper_weight,
        "minimum_track_width_mm": track_width,
        "minimum_clearance_mm": clearance,
    }


def _capability_range_payload(value: Any, context: str) -> dict[str, float] | None:
    if value is None:
        return None
    if isinstance(value, dict):
        return dict(value)
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise ValueError(f"{context} must be (minimum_mm, maximum_mm)")
    return {"minimum_mm": float(value[0]), "maximum_mm": float(value[1])}


def _capability_range_args(payload: dict[str, Any], name: str) -> tuple[float, float] | None:
    value = payload.get(name)
    if value is None:
        return None
    return (value["minimum_mm"], value["maximum_mm"])


def _capability_payload_to_args(payload: dict[str, Any]) -> dict[str, Any]:
    provenance = payload["provenance"]
    return {
        "name": payload["name"],
        "source": provenance["source"],
        "as_of": provenance["as_of"],
        "minimum_track_width": payload["minimum_track_width_mm"],
        "minimum_via_drill": payload["minimum_via_drill_mm"],
        "minimum_via_annular": payload["minimum_via_annular_mm"],
        "minimum_clearances": tuple(
            (entry["first"], entry["second"], entry["clearance_mm"])
            for entry in payload["minimum_clearances"]
        ),
        "copper_weight_refinements": tuple(
            (
                entry["copper_weight_oz"],
                entry["minimum_track_width_mm"],
                entry["minimum_clearance_mm"],
            )
            for entry in payload.get("copper_weight_refinements", ())
        ),
        "supported_copper_layer_counts": tuple(
            payload.get("supported_copper_layer_counts", ())
        ),
        "board_thickness_range": _capability_range_args(payload, "board_thickness_range_mm"),
        "available_copper_weights": tuple(payload.get("available_copper_weights_oz", ())),
        "drill_diameter_range": _capability_range_args(payload, "drill_diameter_range_mm"),
    }


def _canonical_capability_payload(payload: dict[str, Any]) -> dict[str, Any]:
    try:
        return dict(_volt.normalize_capability_profile(payload))
    except ValueError:
        raise
    except Exception as error:
        raise ValueError("Invalid capability profile") from error


def _capability_payload(
    *,
    name: str,
    source: str,
    as_of: str,
    minimum_track_width: float,
    minimum_via_drill: float,
    minimum_via_annular: float,
    minimum_clearances: Iterable[Any],
    copper_weight_refinements: Iterable[Any],
    supported_copper_layer_counts: Iterable[int],
    board_thickness_range: Any,
    available_copper_weights: Iterable[float],
    drill_diameter_range: Any,
) -> dict[str, Any]:
    payload = {
        "name": name,
        "provenance": {"source": source, "as_of": as_of},
        "minimum_track_width_mm": minimum_track_width,
        "minimum_via_drill_mm": minimum_via_drill,
        "minimum_via_annular_mm": minimum_via_annular,
        "minimum_clearances": [
            _capability_clearance_payload(entry) for entry in minimum_clearances
        ],
        "copper_weight_refinements": [
            _capability_refinement_payload(entry) for entry in copper_weight_refinements
        ],
    }
    supported_counts = tuple(supported_copper_layer_counts)
    if supported_counts:
        payload["supported_copper_layer_counts"] = list(supported_counts)
    board_thickness_payload = _capability_range_payload(
        board_thickness_range, "Capability profile board_thickness_range"
    )
    if board_thickness_payload is not None:
        payload["board_thickness_range_mm"] = board_thickness_payload
    weights = tuple(available_copper_weights)
    if weights:
        payload["available_copper_weights_oz"] = list(weights)
    drill_range_payload = _capability_range_payload(
        drill_diameter_range, "Capability profile drill_diameter_range"
    )
    if drill_range_payload is not None:
        payload["drill_diameter_range_mm"] = drill_range_payload
    return payload


def _set_capability_attributes(instance, payload: dict[str, Any]) -> None:
    args = _capability_payload_to_args(payload)
    for name, value in args.items():
        object.__setattr__(instance, name, value)


def _capability_profile_from_payload(payload: dict[str, Any]) -> "CapabilityProfile":
    canonical = _canonical_capability_payload(payload)
    profile = object.__new__(CapabilityProfile)
    _set_capability_attributes(profile, canonical)
    return profile


@dataclass(frozen=True)
class CapabilityProfile:
    """Manufacturer capability profile snapshot with required provenance."""

    name: str
    source: str
    as_of: str
    minimum_track_width: float
    minimum_via_drill: float
    minimum_via_annular: float
    minimum_clearances: tuple[tuple[str, str, float], ...] = ()
    copper_weight_refinements: tuple[tuple[float, float, float], ...] = ()
    supported_copper_layer_counts: tuple[int, ...] = ()
    board_thickness_range: tuple[float, float] | None = None
    available_copper_weights: tuple[float, ...] = ()
    drill_diameter_range: tuple[float, float] | None = None

    def __post_init__(self) -> None:
        canonical = _canonical_capability_payload(
            _capability_payload(
                name=self.name,
                source=self.source,
                as_of=self.as_of,
                minimum_track_width=self.minimum_track_width,
                minimum_via_drill=self.minimum_via_drill,
                minimum_via_annular=self.minimum_via_annular,
                minimum_clearances=self.minimum_clearances,
                copper_weight_refinements=self.copper_weight_refinements,
                supported_copper_layer_counts=self.supported_copper_layer_counts,
                board_thickness_range=self.board_thickness_range,
                available_copper_weights=self.available_copper_weights,
                drill_diameter_range=self.drill_diameter_range,
            )
        )
        _set_capability_attributes(self, canonical)

    @classmethod
    def from_file(cls, path: str | Path) -> "CapabilityProfile":
        """Load a standalone Volt capability profile document."""
        try:
            payload = dict(_volt.read_capability_profile_text(Path(path).read_text(encoding="utf-8")))
        except ValueError:
            raise
        except Exception as error:
            raise ValueError("Invalid capability profile document") from error
        return _capability_profile_from_payload(payload)

    def _to_dict(self) -> dict[str, Any]:
        return _capability_payload(
            name=self.name,
            source=self.source,
            as_of=self.as_of,
            minimum_track_width=self.minimum_track_width,
            minimum_via_drill=self.minimum_via_drill,
            minimum_via_annular=self.minimum_via_annular,
            minimum_clearances=self.minimum_clearances,
            copper_weight_refinements=self.copper_weight_refinements,
            supported_copper_layer_counts=self.supported_copper_layer_counts,
            board_thickness_range=self.board_thickness_range,
            available_copper_weights=self.available_copper_weights,
            drill_diameter_range=self.drill_diameter_range,
        )


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
class _BoardPlacementRef:
    """Internal typed view of one board component placement."""

    index: int
    component: int
    position: Point
    rotation_deg: float
    side: str
    locked: bool


@dataclass(frozen=True)
class _BoardPlacedModel3DRef:
    """Internal typed view of one board placement with selected-part 3D metadata."""

    placement: _BoardPlacementRef
    reference: str
    model: _SelectedPartModel3D | None
    source_path: Path | None
    surface_z: float


@dataclass(frozen=True)
class ComponentFootprintPad:
    """Resolved local footprint pad geometry for a selected component part."""

    pad: int
    pad_label: str
    position: Point
    pin: int | None


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
class Circle:
    """Generic circular board-side primitive."""

    center: Point
    diameter: float
    label: str = ""
    side: str = "top"
    role: str = ""

    def __post_init__(self) -> None:
        object.__setattr__(self, "center", _point(self.center, "Board circle center"))


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

    def set_capability_profile(self, profile: CapabilityProfile) -> Board:
        """Set the board's pinned manufacturer capability profile snapshot."""
        if not isinstance(profile, CapabilityProfile):
            raise TypeError("set_capability_profile expects a CapabilityProfile")
        self._design._circuit.board_set_capability_profile(profile._to_dict())
        return self

    def add_layer(
        self,
        name: str,
        *,
        role: str,
        side: str,
        thickness: float = 0.0,
        enabled: bool = True,
        copper_weight: float | None = None,
    ) -> int:
        """Add a physical or logical board layer and return its kernel index."""
        return self._design._circuit.board_add_layer(
            name,
            role,
            side,
            float(thickness),
            enabled,
            None if copper_weight is None else float(copper_weight),
        )

    def set_layer_stack(
        self,
        layers: Iterable[int],
        *,
        thickness: float,
        dielectrics: Iterable[tuple[float, float]] | None = None,
    ) -> Board:
        """Set the stack order, total thickness, and copper-to-copper dielectrics."""
        self._design._circuit.board_set_layer_stack(
            [_layer_index(layer) for layer in layers],
            float(thickness),
            [
                (float(thickness_mm), float(permittivity))
                for thickness_mm, permittivity in (dielectrics or [])
            ],
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

    @property
    def center(self):
        """Return the center anchor of the board outline bounding box."""
        from ._pcb_layout import center_point

        return center_point(self)

    def edge(self, name: str):
        """Return a mechanical board edge helper."""
        from ._pcb_layout import BoardEdge

        return BoardEdge(self, name)

    def corner(self, name: str):
        """Return a mechanical board corner anchor."""
        from ._pcb_layout import corner_point

        return corner_point(self, name)

    def layout(
        self,
        *,
        at: Point = (0, 0),
        direction: str = "Right",
        unit: float = 1.0,
        grid: float | None = None,
    ):
        """Create a schematic-style PCB placement authoring session."""
        from ._pcb_layout import BoardLayout

        return BoardLayout(self, at=at, direction=direction, unit=unit, grid=grid)

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
        if isinstance(primitive, Circle):
            x, y = primitive.center
            return self._design._circuit.board_add_circle(
                primitive.label, x, y, float(primitive.diameter), primitive.side, primitive.role
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

    def _placements(self) -> tuple[_BoardPlacementRef, ...]:
        return tuple(
            _BoardPlacementRef(
                index=item["index"],
                component=item["component"],
                position=_point(tuple(item["position"]), "Board placement position"),
                rotation_deg=float(item["rotation_deg"]),
                side=item["side"],
                locked=bool(item["locked"]),
            )
            for item in self._design._circuit.board_placement_refs()
        )

    def _surface_z(self, side: str) -> float:
        for layer in self._design._circuit.board_stackup():
            if layer["side"] == side:
                return float(layer["z_mm"])
        return 0.0

    def _placed_model_3d_refs(self) -> tuple[_BoardPlacedModel3DRef, ...]:
        return tuple(
            _BoardPlacedModel3DRef(
                placement=placement,
                reference=self._design._component_reference(placement.component),
                model=self._design._selected_part_model_3d(placement.component),
                source_path=self._design._component_model_3d_asset_source(placement.component),
                surface_z=self._surface_z(placement.side),
            )
            for placement in self._placements()
        )

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

    def assisted_connect(
        self,
        net: Net | int,
        *,
        start: Point,
        start_layer: int,
        end: Point,
        end_layer: int,
    ) -> dict:
        """Route a net between two points using the kernel assisted-connection solver.

        Returns the kernel solver result: a dict with a ``routed`` flag, created
        ``tracks`` and ``vias`` ids, and failure ``blockers``. All routing geometry is
        decided by the kernel.
        """
        if isinstance(net, Net):
            if net._design is not self._design:
                raise ValueError("Net belongs to a different design")
            net_index = net.index
        else:
            net_index = _net_index(net)
        start_x, start_y = _point(start, "Assisted connection start")
        end_x, end_y = _point(end, "Assisted connection end")
        return self._design._circuit.board_assisted_connect(
            net_index,
            start_x,
            start_y,
            _layer_index(start_layer),
            end_x,
            end_y,
            _layer_index(end_layer),
        )

    def escape(self, component: Component | int) -> dict:
        """Fan out a placed component using the kernel escape router."""
        if isinstance(component, Component):
            if component._design is not self._design:
                raise ValueError("Component belongs to a different design")
            component_index = component.index
        else:
            component_index = _component_index(component)
        self._sync_object_footprints()
        return self._design._circuit.board_escape(component_index)

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

    def add_room(
        self,
        name: str,
        *,
        outline: Iterable[Point],
        layers: Iterable[int],
        clearance: float | None = None,
        track_width: float | None = None,
        priority: int = 0,
    ) -> int:
        """Add a named board room with optional local routing rule overrides."""
        return self._design._circuit.board_add_room(
            name,
            [_point(point, "Board room outline point") for point in outline],
            _layer_indices(layers),
            None if clearance is None else float(clearance),
            None if track_width is None else float(track_width),
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

    def to_svg(
        self,
        *,
        pad_net_overlays: bool = True,
        diagnostic_overlays: bool = True,
        ratsnest_edges: bool = True,
        layer: int | None = None,
    ) -> str:
        """Render the PCB projection as SVG."""
        self._sync_object_footprints()
        return self._design._circuit.board_to_svg(
            pad_net_overlays,
            diagnostic_overlays,
            ratsnest_edges,
            None if layer is None else _layer_index(layer),
        )

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

    def _component_footprint_pads(self, component: int) -> tuple[ComponentFootprintPad, ...]:
        self._sync_component_object_footprint(component)
        return tuple(
            ComponentFootprintPad(
                pad=item["pad"],
                pad_label=item["pad_label"],
                position=_point(tuple(item["position"]), "Component footprint pad position"),
                pin=item["pin"],
            )
            for item in self._design._circuit.board_component_footprint_pads(component)
        )

    def _outline_vertices(self) -> tuple[Point, ...]:
        vertices = self._design._circuit.board_outline_vertices()
        if not vertices:
            raise ValueError("Board mechanical anchors require a board outline")
        return tuple(
            _point(tuple(vertex), "Board outline vertex")
            for vertex in vertices
        )

    def _outline_bbox(self) -> tuple[float, float, float, float]:
        vertices = self._outline_vertices()
        xs = tuple(vertex[0] for vertex in vertices)
        ys = tuple(vertex[1] for vertex in vertices)
        return (min(xs), min(ys), max(xs), max(ys))

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
