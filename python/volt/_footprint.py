"""Public footprint value object shared by logical and PCB Python facades."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

FootprintRef = tuple[str, str]
FootprintPolygon = tuple[tuple[float, float], ...]


@dataclass(frozen=True, init=False)
class FootprintMarking:
    """Semantic non-pad marking geometry owned by a footprint."""

    kind: str
    polygon: FootprintPolygon

    def __init__(self, kind: str, polygon: Iterable[tuple[float, float]]) -> None:
        if kind not in {"silkscreen", "polarity", "pin_1"}:
            raise ValueError("Footprint marking kind must be silkscreen, polarity, or pin_1")
        object.__setattr__(self, "kind", kind)
        object.__setattr__(self, "polygon", _footprint_polygon(polygon, "marking"))

    @classmethod
    def silkscreen(cls, polygon: Iterable[tuple[float, float]]) -> FootprintMarking:
        """Return a footprint-owned silkscreen marking polygon."""
        return cls("silkscreen", polygon)

    @classmethod
    def polarity(cls, polygon: Iterable[tuple[float, float]]) -> FootprintMarking:
        """Return a footprint-owned polarity marking polygon."""
        return cls("polarity", polygon)

    @classmethod
    def pin_1(cls, polygon: Iterable[tuple[float, float]]) -> FootprintMarking:
        """Return a footprint-owned pin-1 marking polygon."""
        return cls("pin_1", polygon)

    def _to_dict(self) -> dict:
        return {"kind": self.kind, "polygon": list(self.polygon)}


@dataclass(frozen=True, init=False)
class Footprint:
    """Library-qualified footprint geometry for board-ready physical parts."""

    ref: FootprintRef
    pads: tuple[Any, ...]
    courtyard: FootprintPolygon | None
    body: FootprintPolygon | None
    fabrication_outline: FootprintPolygon | None
    assembly_outline: FootprintPolygon | None
    markings: tuple[FootprintMarking, ...]

    def __init__(
        self,
        ref: FootprintRef | None = None,
        *,
        library: str | None = None,
        name: str | None = None,
        pads: Iterable[Any],
        courtyard: Iterable[tuple[float, float]] | None = None,
        body: Iterable[tuple[float, float]] | None = None,
        fabrication_outline: Iterable[tuple[float, float]] | None = None,
        assembly_outline: Iterable[tuple[float, float]] | None = None,
        markings: Iterable[FootprintMarking] = (),
    ) -> None:
        if ref is not None:
            if library is not None or name is not None:
                raise TypeError("Footprint accepts either ref or library/name identity")
            library, name = _tuple_footprint_ref(ref)
        elif library is None or name is None:
            raise TypeError("Footprint requires library and name")

        if not isinstance(library, str) or not isinstance(name, str):
            raise TypeError("Footprint library and name must be strings")
        if not library:
            raise ValueError("Footprint library must not be empty")
        if not name:
            raise ValueError("Footprint name must not be empty")

        object.__setattr__(self, "ref", (library, name))
        object.__setattr__(self, "pads", tuple(pads))
        object.__setattr__(self, "courtyard", _footprint_polygon(courtyard, "courtyard"))
        object.__setattr__(self, "body", _footprint_polygon(body, "body"))
        object.__setattr__(
            self,
            "fabrication_outline",
            _footprint_polygon(fabrication_outline, "fabrication_outline"),
        )
        object.__setattr__(
            self,
            "assembly_outline",
            _footprint_polygon(assembly_outline, "assembly_outline"),
        )
        object.__setattr__(self, "markings", _footprint_markings(markings))

    @property
    def library(self) -> str:
        """Return the footprint library name from the footprint reference."""
        return self.ref[0]

    @property
    def name(self) -> str:
        """Return the footprint name from the footprint reference."""
        return self.ref[1]

    def _to_dict(self) -> dict:
        payload = {"ref": self.ref, "pads": [pad._to_dict() for pad in self.pads]}
        if self.courtyard is not None:
            payload["courtyard"] = list(self.courtyard)
        if self.body is not None:
            payload["body"] = list(self.body)
        if self.fabrication_outline is not None:
            payload["fabrication_outline"] = list(self.fabrication_outline)
        if self.assembly_outline is not None:
            payload["assembly_outline"] = list(self.assembly_outline)
        if self.markings:
            payload["markings"] = [marking._to_dict() for marking in self.markings]
        return payload


FootprintInput = Footprint | FootprintRef


def footprint_ref(footprint: FootprintInput) -> FootprintRef:
    if isinstance(footprint, Footprint):
        return footprint.ref
    if isinstance(footprint, tuple):
        return _tuple_footprint_ref(footprint)
    raise TypeError("footprint must be a Footprint or (library, name) tuple")


def _tuple_footprint_ref(ref: tuple[object, ...]) -> FootprintRef:
    if len(ref) != 2:
        raise TypeError("footprint must be a Footprint or (library, name) tuple")
    library, name = ref
    if not isinstance(library, str) or not isinstance(name, str):
        raise TypeError("Footprint library and name must be strings")
    if not library:
        raise ValueError("Footprint library must not be empty")
    if not name:
        raise ValueError("Footprint name must not be empty")
    return library, name


def _footprint_polygon(
    value: Iterable[tuple[float, float]] | None,
    field_name: str,
) -> FootprintPolygon | None:
    if value is None:
        return None
    vertices: list[tuple[float, float]] = []
    for point in value:
        coordinates = tuple(point)
        if len(coordinates) != 2:
            raise TypeError(f"Footprint {field_name} points must be (x, y) pairs")
        x, y = coordinates
        vertices.append((float(x), float(y)))
    return tuple(vertices)


def _footprint_markings(markings: Iterable[FootprintMarking]) -> tuple[FootprintMarking, ...]:
    result = tuple(markings)
    if not all(isinstance(marking, FootprintMarking) for marking in result):
        raise TypeError("Footprint markings must contain FootprintMarking values")
    return result
