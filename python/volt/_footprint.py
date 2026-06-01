"""Public footprint value object shared by logical and PCB Python facades."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

FootprintRef = tuple[str, str]


@dataclass(frozen=True, init=False)
class Footprint:
    """Library-qualified footprint geometry for board-ready physical parts."""

    ref: FootprintRef
    pads: tuple[Any, ...]

    def __init__(
        self,
        ref: FootprintRef | None = None,
        *,
        library: str | None = None,
        name: str | None = None,
        pads: Iterable[Any],
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

    @property
    def library(self) -> str:
        """Return the footprint library name from the footprint reference."""
        return self.ref[0]

    @property
    def name(self) -> str:
        """Return the footprint name from the footprint reference."""
        return self.ref[1]

    def _to_dict(self) -> dict:
        return {"ref": self.ref, "pads": [pad._to_dict() for pad in self.pads]}


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
