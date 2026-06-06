"""Diagnostic value objects for the Volt Python facade."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Iterator

PCB_VISUAL_DIAGNOSTIC_CODES: tuple[str, ...] = (
    "PCB_VISUAL_PLACEMENT_OVERLAP",
    "PCB_VISUAL_PLACEMENT_CROWDING",
    "PCB_VISUAL_REFERENCE_DESIGNATOR_HIDDEN",
    "PCB_VISUAL_REFERENCE_DESIGNATOR_UNREADABLE",
    "PCB_VISUAL_LABEL_OVERLAP",
    "PCB_VISUAL_ROUTE_READABILITY_CONFLICT",
    "PCB_VISUAL_BOARD_FEATURE_ANNOTATION_MISSING",
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


@dataclass(frozen=True)
class Diagnostic:
    """Kernel-produced diagnostic exposed through the Python facade."""

    severity: str
    code: str
    message: str
    entities: tuple[DiagnosticEntity, ...]
    category: str = "general"
    overlays: tuple[DiagnosticOverlay, ...] = ()


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
