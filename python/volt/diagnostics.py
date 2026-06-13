"""Diagnostic value objects for the Volt Python facade."""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Iterable, Iterator

from . import _volt

DIAGNOSTIC_CATEGORIES: tuple[str, ...] = tuple(_volt.diagnostic_categories())
ERC_DIAGNOSTIC_CODES: tuple[str, ...] = tuple(_volt.erc_diagnostic_codes())
DRC_DIAGNOSTIC_CODES: tuple[str, ...] = tuple(_volt.drc_diagnostic_codes())
PCB_VISUAL_DIAGNOSTIC_CODES: tuple[str, ...] = tuple(_volt.pcb_visual_diagnostic_codes())
PCB_FABRICATION_DIAGNOSTIC_CODES: tuple[str, ...] = tuple(
    _volt.pcb_fabrication_diagnostic_codes()
)


@dataclass(frozen=True)
class DiagnosticEntity:
    """Reference to a kernel entity involved in a diagnostic."""

    kind: str
    index: int


@dataclass(frozen=True)
class DiagnosticOverlay:
    """Overlay-ready geometry and references attached to a diagnostic."""

    kind: str
    points: tuple[tuple[float, float], ...]
    entities: tuple[DiagnosticEntity, ...] = ()
    layers: tuple[DiagnosticEntity, ...] = ()

    def __post_init__(self) -> None:
        points = tuple(_diagnostic_overlay_point(point) for point in self.points)
        _validate_diagnostic_overlay_shape(self.kind, points)
        layers = tuple(self.layers)
        for layer in layers:
            if layer.kind != "board_layer":
                raise ValueError("Diagnostic overlay layers must be board_layer references")
        object.__setattr__(self, "points", points)
        object.__setattr__(self, "entities", tuple(self.entities))
        object.__setattr__(self, "layers", layers)


@dataclass(frozen=True)
class DiagnosticMeasurement:
    """Typed actual-versus-required measurement attached to a diagnostic."""

    actual_mm: float
    required_mm: float


@dataclass(frozen=True)
class Diagnostic:
    """Kernel-produced diagnostic exposed through the Python facade."""

    severity: str
    code: str
    message: str
    entities: tuple[DiagnosticEntity, ...]
    category: str = "general"
    overlays: tuple[DiagnosticOverlay, ...] = ()
    measurement: DiagnosticMeasurement | None = None
    rule: str | None = None


class DiagnosticReport:
    """Ordered collection of kernel diagnostics."""

    def __init__(self, diagnostics: Iterable[Diagnostic]):
        self._diagnostics = tuple(diagnostics)

    def __iter__(self) -> Iterator[Diagnostic]:
        """Iterate over diagnostics in report order."""
        return iter(self._diagnostics)

    def __len__(self) -> int:
        """Return the number of diagnostics in this report."""
        return len(self._diagnostics)

    def __getitem__(self, index: int) -> Diagnostic:
        """Return one diagnostic by positional index."""
        return self._diagnostics[index]

    @property
    def has_errors(self) -> bool:
        """Return whether this report contains at least one error diagnostic."""
        return any(diagnostic.severity == "error" for diagnostic in self._diagnostics)



def _diagnostic_from_dict(item) -> Diagnostic:
    return Diagnostic(
        severity=item["severity"],
        code=item["code"],
        message=item["message"],
        entities=tuple(
            DiagnosticEntity(entity["kind"], entity["index"]) for entity in item["entities"]
        ),
        category=item.get("category", "general"),
        overlays=tuple(
            _diagnostic_overlay_from_dict(overlay)
            for overlay in item.get("overlays", ())
        ),
        measurement=_diagnostic_measurement_from_dict(item.get("measurement")),
        rule=item.get("rule"),
    )


def _diagnostic_measurement_from_dict(item) -> DiagnosticMeasurement | None:
    if item is None:
        return None
    return DiagnosticMeasurement(
        actual_mm=float(item["actual_mm"]),
        required_mm=float(item["required_mm"]),
    )


def _diagnostic_overlay_from_dict(item) -> DiagnosticOverlay:
    return DiagnosticOverlay(
        kind=item["kind"],
        points=tuple((float(point[0]), float(point[1])) for point in item["points"]),
        entities=tuple(
            DiagnosticEntity(entity["kind"], entity["index"])
            for entity in item.get("entities", ())
        ),
        layers=tuple(
            DiagnosticEntity(entity["kind"], entity["index"])
            for entity in item.get("layers", ())
        ),
    )


def _diagnostic_overlay_point(point) -> tuple[float, float]:
    x = float(point[0])
    y = float(point[1])
    if not math.isfinite(x) or not math.isfinite(y):
        raise ValueError("Diagnostic overlay points must be finite")
    return (x, y)


def _validate_diagnostic_overlay_shape(
    kind: str,
    points: tuple[tuple[float, float], ...],
) -> None:
    if kind in {"bounding_box", "segment"}:
        if len(points) != 2:
            raise ValueError("Diagnostic bounding boxes and segments require two points")
        return
    if kind == "point":
        if len(points) != 1:
            raise ValueError("Diagnostic point overlays require one point")
        return
    if kind == "polygon":
        if len(points) < 3:
            raise ValueError("Diagnostic polygon overlays require at least three points")
        return
    raise ValueError(f"Unsupported diagnostic overlay kind: {kind}")
