"""Internal algorithms for generic PCB layout composition."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from ._utils import _coordinate, _positive_coordinate

if TYPE_CHECKING:
    from ._pcb_layout import BoardAnchor, BoardLayout
    from .logical import Net


@dataclass(frozen=True)
class BoardFanout:
    """Result of one authored fanout route and optional via."""

    source: BoardAnchor
    end: BoardAnchor
    track: int
    via: int | None = None


def rule(layout: BoardLayout, name: str) -> float:
    """Return a board design-rule value by a compact authoring name."""
    if not isinstance(name, str):
        raise TypeError("Board layout rule names must be strings")
    normalized = name.casefold().replace("-", "_")
    aliases = {
        "clearance": "copper_clearance_mm",
        "copper_clearance": "copper_clearance_mm",
        "copper_clearance_mm": "copper_clearance_mm",
        "track_width": "minimum_track_width_mm",
        "min_track_width": "minimum_track_width_mm",
        "minimum_track_width": "minimum_track_width_mm",
        "minimum_track_width_mm": "minimum_track_width_mm",
        "via_drill": "minimum_via_drill_diameter_mm",
        "min_via_drill": "minimum_via_drill_diameter_mm",
        "minimum_via_drill": "minimum_via_drill_diameter_mm",
        "minimum_via_drill_diameter_mm": "minimum_via_drill_diameter_mm",
        "via_annular": "minimum_via_annular_diameter_mm",
        "min_via_annular": "minimum_via_annular_diameter_mm",
        "minimum_via_annular": "minimum_via_annular_diameter_mm",
        "minimum_via_annular_diameter_mm": "minimum_via_annular_diameter_mm",
        "outline_clearance": "board_outline_clearance_mm",
        "board_outline_clearance": "board_outline_clearance_mm",
        "board_outline_clearance_mm": "board_outline_clearance_mm",
    }
    key = aliases.get(normalized)
    if key is None:
        raise ValueError(f"Unknown board layout rule {name!r}")
    return float(layout._board.design_rules()[key])


def align(
    layout: BoardLayout,
    anchors,
    *,
    axis: str,
    target: tuple[float, float] | BoardAnchor | float | None = None,
) -> tuple[BoardAnchor, ...]:
    """Return anchors aligned along one board axis."""
    items = tuple(layout._anchor_at(anchor) for anchor in anchors)
    if not items:
        return ()
    normalized_axis = _axis(axis)
    coordinate = (
        items[0].x
        if normalized_axis == "x" and target is None
        else items[0].y
        if target is None
        else _axis_target(layout, target, normalized_axis)
    )
    return tuple(
        anchor.tox(coordinate) if normalized_axis == "x" else anchor.toy(coordinate)
        for anchor in items
    )


def distribute(
    layout: BoardLayout,
    *,
    count: int,
    start: tuple[float, float] | BoardAnchor,
    end: tuple[float, float] | BoardAnchor,
) -> tuple[BoardAnchor, ...]:
    """Return evenly distributed anchors between two endpoints."""
    if isinstance(count, bool) or not isinstance(count, int):
        raise TypeError("Board layout distribute count must be an integer")
    if count < 0:
        raise ValueError("Board layout distribute count must not be negative")
    if count == 0:
        return ()
    first = layout._anchor_at(start)
    last = layout._anchor_at(end)
    if count == 1:
        return (first,)
    anchors = []
    for index in range(count):
        fraction = index / (count - 1)
        anchors.append(
            layout._snap_anchor(
                first.offset(
                    dx=(last.x - first.x) * fraction,
                    dy=(last.y - first.y) * fraction,
                )
            )
        )
    return tuple(anchors)


def mirror(
    layout: BoardLayout,
    anchors,
    *,
    axis: str,
    about: tuple[float, float] | BoardAnchor | float | None = None,
) -> tuple[BoardAnchor, ...]:
    """Return anchors mirrored across a board x or y axis."""
    normalized_axis = _axis(axis)
    coordinate = (
        (layout.here.x if normalized_axis == "x" else layout.here.y)
        if about is None
        else _axis_target(layout, about, normalized_axis)
    )
    result = []
    for anchor in anchors:
        item = layout._anchor_at(anchor)
        mirrored = (
            item.tox((2.0 * coordinate) - item.x)
            if normalized_axis == "x"
            else item.toy((2.0 * coordinate) - item.y)
        )
        result.append(layout._snap_anchor(mirrored))
    return tuple(result)


def connect(
    layout: BoardLayout,
    start: tuple[float, float] | BoardAnchor,
    end: tuple[float, float] | BoardAnchor,
    *,
    layer: int,
    net: Net | int | None = None,
    width: float | None = None,
    through=(),
    mode: str = "octilinear",
) -> int:
    """Route between two anchors, inferring the net from pad anchors when possible."""
    route_net = _route_net(net, start, end)
    route = layout.route(route_net, layer=layer, width=width, mode=mode).at(start)
    for anchor in _anchor_collection(through, "Board layout connect through"):
        route.through(anchor)
    return route.to(end)


def bundle(
    layout: BoardLayout,
    pairs,
    *,
    layer: int,
    net: Net | int | None = None,
    width: float | None = None,
    mode: str = "octilinear",
) -> tuple[int, ...]:
    """Route multiple independent anchor pairs as one deterministic bundle."""
    tracks = []
    for item in pairs:
        start, end, through = _bundle_pair(item)
        tracks.append(
            layout.connect(
                start,
                end,
                layer=layer,
                net=net,
                width=width,
                through=through,
                mode=mode,
            )
        )
    return tuple(tracks)


def stitch(
    layout: BoardLayout,
    net: Net | int,
    *,
    at,
    start_layer: int,
    end_layer: int,
    drill: float | None = None,
    annular: float | None = None,
) -> tuple[int, ...]:
    """Add a deterministic set of vias for one net."""
    return tuple(
        layout.via(
            net,
            at=anchor,
            start_layer=start_layer,
            end_layer=end_layer,
            drill=drill,
            annular=annular,
        )
        for anchor in _anchor_collection(at, "Board layout stitch at")
    )


def fanout(
    layout: BoardLayout,
    anchors,
    *,
    layer: int,
    direction: str,
    distance: float,
    net: Net | int | None = None,
    width: float | None = None,
    via_layers: tuple[int, int] | None = None,
    drill: float | None = None,
    annular: float | None = None,
) -> tuple[BoardFanout, ...]:
    """Route one or more anchors outward and optionally drop vias at the endpoints."""
    fanout_direction = _direction(direction)
    fanout_distance = _positive_coordinate(distance, "Board fanout distance")
    dx, dy = _direction_offset(fanout_direction, fanout_distance)
    results = []
    for source in _anchor_collection(anchors, "Board layout fanout anchors"):
        source_anchor = layout._anchor_at(source)
        end = source_anchor.offset(dx=dx, dy=dy)
        route_net = net if net is not None else _anchor_net(source)
        if route_net is None:
            raise ValueError("Board layout fanout requires a net or pad anchors with nets")
        track = layout.connect(source_anchor, end, layer=layer, net=route_net, width=width)
        via = None
        if via_layers is not None:
            start_layer, end_layer = _via_layer_pair(via_layers)
            via = layout.via(
                route_net,
                at=end,
                start_layer=start_layer,
                end_layer=end_layer,
                drill=drill,
                annular=annular,
            )
        results.append(BoardFanout(source_anchor, end, track, via))
    return tuple(results)


def polygon(layout: BoardLayout, vertices) -> tuple[BoardAnchor, ...]:
    """Return a polygon outline from board anchors or local coordinates."""
    return tuple(layout._anchor_at(vertex) for vertex in vertices)


def rect(
    layout: BoardLayout,
    *,
    at: tuple[float, float] | BoardAnchor | None = None,
    size: tuple[float, float],
) -> tuple[BoardAnchor, ...]:
    """Return a rectangular outline from a board anchor and size."""
    base = layout.here if at is None else layout._anchor_at(at)
    width, height = _board_size_tuple(size, "Board layout rectangle size")
    snap_generated = not _is_anchor(at)

    def make_anchor(anchor: BoardAnchor) -> BoardAnchor:
        return layout._snap_anchor(anchor) if snap_generated else anchor

    return (
        base,
        make_anchor(base.offset(dx=width)),
        make_anchor(base.offset(dx=width, dy=height)),
        make_anchor(base.offset(dy=height)),
    )


def zone(
    layout: BoardLayout,
    *,
    layers,
    net: Net | int | None = None,
    outline=None,
    at: tuple[float, float] | BoardAnchor | None = None,
    size: tuple[float, float] | None = None,
    fill: str = "solid",
    priority: int = 0,
) -> int:
    """Add a copper zone from layout-authored geometry."""
    anchors = _outline_anchors(layout, outline=outline, at=at, size=size)
    return layout._board.add_zone(
        outline=tuple(anchor.point for anchor in anchors),
        layers=layers,
        net=net,
        fill=fill,
        priority=priority,
    )


def keepout(
    layout: BoardLayout,
    *,
    layers,
    restrictions,
    outline=None,
    at: tuple[float, float] | BoardAnchor | None = None,
    size: tuple[float, float] | None = None,
) -> int:
    """Add a keepout from layout-authored geometry."""
    anchors = _outline_anchors(layout, outline=outline, at=at, size=size)
    return layout._board.add_keepout(
        outline=tuple(anchor.point for anchor in anchors),
        layers=layers,
        restrictions=restrictions,
    )


def text(
    layout: BoardLayout,
    value: str,
    *,
    at: tuple[float, float] | BoardAnchor,
    layer: int,
    rotation: float = 0.0,
    size: float = 1.0,
    locked: bool = False,
) -> int:
    """Add board text at a layout anchor."""
    anchor = layout._anchor_at(at)
    return layout._board.add_text(
        value,
        at=anchor.point,
        layer=layer,
        rotation=rotation,
        size=size,
        locked=locked,
    )


def track_width(layout: BoardLayout, width: float | None) -> float:
    """Resolve an authored track width against board design rules."""
    if width is not None:
        return _positive_coordinate(width, "Board route width")
    return max(0.20, rule(layout, "minimum_track_width_mm"))


def via_drill(layout: BoardLayout, drill: float | None) -> float:
    """Resolve an authored via drill against board design rules."""
    if drill is not None:
        return _positive_coordinate(drill, "Board via drill")
    return max(0.30, rule(layout, "minimum_via_drill_diameter_mm"))


def via_annular(layout: BoardLayout, annular: float | None) -> float:
    """Resolve an authored via annular diameter against board design rules."""
    if annular is not None:
        return _positive_coordinate(annular, "Board via annular")
    return max(0.70, rule(layout, "minimum_via_annular_diameter_mm"))


def _outline_anchors(
    layout: BoardLayout,
    *,
    outline,
    at: tuple[float, float] | BoardAnchor | None,
    size: tuple[float, float] | None,
) -> tuple[BoardAnchor, ...]:
    if outline is not None:
        if at is not None or size is not None:
            raise ValueError("Board layout outline cannot be combined with at/size")
        return polygon(layout, outline)
    if size is None:
        raise ValueError("Board layout outline requires either outline or size")
    return rect(layout, at=at, size=size)


def _axis_target(layout: BoardLayout, target, axis: str) -> float:
    if _is_anchor(target):
        if target._board is not layout._board:
            raise ValueError("Board anchor belongs to a different board")
        return target.x if axis == "x" else target.y
    return layout._snap_coordinate(_coordinate(target))


def _route_net(net: Net | int | None, start, end) -> Net | int:
    if net is not None:
        return net
    start_net = _anchor_net(start)
    end_net = _anchor_net(end)
    if start_net is None or end_net is None:
        raise ValueError("Board layout connect requires a net unless both anchors resolve nets")
    if start_net != end_net:
        raise ValueError("Board layout connect anchors resolve to different nets")
    return start_net


def _bundle_pair(value) -> tuple:
    try:
        item = tuple(value)
    except TypeError as exc:
        raise TypeError(
            "Board layout bundle entries must be (start, end) or (start, end, through)"
        ) from exc
    if len(item) == 2:
        return (item[0], item[1], ())
    if len(item) == 3:
        return (item[0], item[1], item[2])
    raise TypeError(
        "Board layout bundle entries must be (start, end) or (start, end, through)"
    )


def _anchor_net(value) -> int | None:
    if hasattr(value, "pad_label") and hasattr(value, "net"):
        return value.net
    return None


def _anchor_collection(value, context: str) -> tuple:
    if _is_anchor(value) or isinstance(value, (str, bytes)):
        raise TypeError(f"{context} must be an iterable of anchors")
    try:
        return tuple(value)
    except TypeError as exc:
        raise TypeError(f"{context} must be an iterable of anchors") from exc


def _is_anchor(value) -> bool:
    return hasattr(value, "point") and hasattr(value, "offset")


def _board_size_tuple(value, context: str) -> tuple[float, float]:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise TypeError(f"{context} must be a (width, height) pair")
    return (
        _positive_coordinate(value[0], f"{context} width"),
        _positive_coordinate(value[1], f"{context} height"),
    )


def _via_layer_pair(value) -> tuple[int, int]:
    if (
        not isinstance(value, (tuple, list))
        or len(value) != 2
        or isinstance(value[0], bool)
        or isinstance(value[1], bool)
        or not isinstance(value[0], int)
        or not isinstance(value[1], int)
    ):
        raise TypeError("Board fanout via_layers must be a pair of layer IDs")
    return (value[0], value[1])


def _axis(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Board layout axis must be a string")
    normalized = value.casefold()
    if normalized in {"x", "horizontal"}:
        return "x"
    if normalized in {"y", "vertical"}:
        return "y"
    raise ValueError("Board layout axis must be x or y")


def _direction(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Board layout direction must be a string")
    normalized = {
        "right": "Right",
        "down": "Down",
        "left": "Left",
        "up": "Up",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError("Board layout direction must be Right, Down, Left, or Up")
    return normalized


def _direction_offset(direction: str, distance: float) -> tuple[float, float]:
    if direction == "Right":
        return (distance, 0.0)
    if direction == "Left":
        return (-distance, 0.0)
    if direction == "Down":
        return (0.0, distance)
    return (0.0, -distance)
