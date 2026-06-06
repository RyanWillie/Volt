"""Shared lookup rules for project result and stage-test model access."""

from __future__ import annotations

from typing import TypeVar

from .pcb import Board
from .schematic import Schematic

T = TypeVar("T")


def one_or_named(
    items: tuple[T, ...],
    name: str | None,
    kind: str,
    owner: str,
) -> T:
    """Return one item by raw name, or the only item in the collection."""
    if name is None:
        if len(items) != 1:
            raise LookupError(f"{owner} has {len(items)} {kind} models")
        return items[0]

    matches = [item for item in items if item.name == name]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        raise LookupError(f"{owner} has multiple {kind} models named {name}")
    raise LookupError(f"{owner} has no {kind} named {name}")


def one_or_named_projection(
    items: tuple[Board, ...] | tuple[Schematic, ...],
    name: str | None,
    kind: str,
    owner: str,
):
    """Return one projection by shared raw-name or composite lookup rules."""
    if name is None:
        if len(items) != 1:
            raise LookupError(f"{owner} has {len(items)} {kind} models")
        return items[0]

    keyed_matches = [item for item in items if model_output_name(item, items) == name]
    if len(keyed_matches) == 1:
        return keyed_matches[0]

    name_matches = [item for item in items if item.name == name]
    if len(name_matches) == 1:
        return name_matches[0]
    if len(name_matches) > 1:
        raise LookupError(f"{owner} has multiple {kind} models named {name}; use design:{kind}")
    raise LookupError(f"{owner} has no {kind} named {name}")


def model_output_name(model: object, siblings: tuple[object, ...]) -> str:
    """Return the canonical lookup/output name for one project model."""
    if isinstance(model, (Board, Schematic)) and len(siblings) > 1:
        return f"{projection_key_part(model._design.name)}:{projection_key_part(model.name)}"
    return model.name


def projection_key_part(name: str) -> str:
    """Escape one projection key segment for composite lookup names."""
    return name.replace("~", "~0").replace(":", "~1")
