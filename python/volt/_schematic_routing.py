"""Wire route geometry helpers for schematic authoring."""

from __future__ import annotations

from typing import Iterable

from ._utils import _coordinate


def _orthogonal_wire_points(
    points: Iterable[tuple[float, float]],
) -> tuple[tuple[float, float], ...]:
    """Return orthogonal route points while preserving explicit author points.

    For a simple two-point diagonal route, insert a single horizontal-then-vertical
    bend. For routes with explicit ``via()`` points, keep the authored path unchanged.
    """
    result = tuple(points)
    if len(result) == 2:
        start, end = result
        if start[0] != end[0] and start[1] != end[1]:
            midpoint = (end[0], start[1])
            return (start, midpoint, end)
    return result


def _normalize_schematic_route_points(
    points: Iterable[tuple[float, float]],
) -> tuple[tuple[float, float], ...]:
    result: list[tuple[float, float]] = []
    for point in points:
        converted = (_coordinate(point[0]), _coordinate(point[1]))
        if not result or result[-1] != converted:
            result.append(converted)
    if len(result) < 2:
        raise ValueError("Schematic wire route must contain at least two distinct points")
    return tuple(result)


def _schematic_route_has_distinct_points(points: Iterable[tuple[float, float]]) -> bool:
    first: tuple[float, float] | None = None
    for point in points:
        if first is None:
            first = point
        elif point != first:
            return True
    return False


def _shape_wire_points(
    start: tuple[float, float],
    end: tuple[float, float],
    *,
    shape: str,
    k: float | None,
) -> tuple[tuple[float, float], ...]:
    if not isinstance(shape, str):
        raise TypeError("Schematic wire shape must be a string")
    normalized_shape = {"n": "|-|", "c": "-|-"}.get(shape, shape)
    valid_shapes = ("-", "-|", "|-", "|-|", "-|-")
    if normalized_shape not in valid_shapes:
        raise ValueError(
            "Schematic wire shape must be one of -, -|, |-, |-|, n, -|-, or c"
        )

    sx, sy = start
    ex, ey = end
    if normalized_shape == "-":
        return _normalize_schematic_route_points((start, end))

    offset = None if k is None else _coordinate(k)
    if normalized_shape == "-|":
        bend_x = ex if offset is None else sx + offset
        return _normalize_schematic_route_points(
            (start, (bend_x, sy), (bend_x, ey), end)
        )
    if normalized_shape == "|-":
        bend_y = ey if offset is None else sy + offset
        return _normalize_schematic_route_points(
            (start, (sx, bend_y), (ex, bend_y), end)
        )
    if normalized_shape == "|-|":
        bend_y = (sy + ey) / 2 if offset is None else sy + offset
        return _normalize_schematic_route_points(
            (start, (sx, bend_y), (ex, bend_y), end)
        )

    bend_x = (sx + ex) / 2 if offset is None else sx + offset
    return _normalize_schematic_route_points(
        (start, (bend_x, sy), (bend_x, ey), end)
    )


def _multi_anchor_orthogonal_wire_points(
    points: Iterable[tuple[float, float]],
) -> tuple[tuple[float, float], ...]:
    anchors = tuple(points)
    if len(anchors) < 2:
        raise ValueError("Schematic wire route must contain at least two distinct points")
    routed: list[tuple[float, float]] = []
    for start, end in zip(anchors, anchors[1:]):
        segment = _orthogonal_wire_points((start, end))
        routed.extend(segment if not routed else segment[1:])
    return _normalize_schematic_route_points(routed)


def _multi_anchor_shape_wire_points(
    points: Iterable[tuple[float, float]],
    *,
    shape: str,
    k: float | None,
) -> tuple[tuple[float, float], ...]:
    anchors = tuple(points)
    if len(anchors) < 2:
        raise ValueError("Schematic wire route must contain at least two distinct points")
    routed: list[tuple[float, float]] = []
    for start, end in zip(anchors, anchors[1:]):
        segment = _shape_wire_points(start, end, shape=shape, k=k)
        routed.extend(segment if not routed else segment[1:])
    return _normalize_schematic_route_points(routed)
