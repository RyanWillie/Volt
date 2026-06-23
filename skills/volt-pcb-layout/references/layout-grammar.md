# Layout Grammar Reference

Full reference for the `board.layout(unit=, grid=)` grammar.

**Source:** `python/volt/_pcb_layout.py` — `BoardLayout`, `BoardRoute`,
`BoardTwoPadComponent`, and `BoardAnchor` classes. Verbatim usage lines are quoted from
`examples/timer_555_led_blinker/board.py` and `examples/pcb_led_board/main.py`.

---

## Opening the layout session

```python
with board.layout(unit=1.0, grid=0.5) as layout:
    ...
```

| Parameter | Purpose |
|---|---|
| `unit` | Default pitch for `stack()` when no `pitch=` is given (mm). |
| `grid` | Snaps explicit `(x, y)` tuple coordinates and derived anchors to this grid (mm). Omit to disable snapping. |

The context manager returns the `BoardLayout` cursor. After the `with` block the layout is
complete; no further methods are callable.

---

## BoardLayout methods

### Cursor state

#### `layout.here` → `BoardAnchor`
Current cursor position. Updated by most placement and route methods.

#### `layout.direction` → `str`
Current cursor direction (`"Right"`, `"Down"`, `"Left"`, `"Up"`). Defaults to `"Right"`.

#### `layout.unit` → `float`
The unit pitch set at construction.

#### `layout.grid` → `float | None`
The snapping grid, if set.

#### `layout.move(dx=0, dy=0)` → `BoardLayout`
Shift the cursor by a relative offset. Returns `self` for chaining.

```python
layout.move(dx=2.0)
```

#### `layout.move_from(anchor, dx=0, dy=0, direction=None)` → `BoardLayout`
Jump cursor to `anchor` plus an optional relative offset and optional new direction.

```python
layout.move_from(timer.VCC, dy=-3.0)
```

---

### Snap / node helpers

#### `layout.snap(point=None)` → `BoardAnchor`
Return the anchor (or current cursor) snapped to the layout grid. Use this on every
derived anchor you will reference in more than one place.

```python
# from examples/timer_555_led_blinker/board.py
timing_junction = layout.snap(rb.end.right(2.0))
power_rail = layout.snap(header[1].up(5.15))
```

#### `layout.snap_x(anchor_or_x)` → `float`
Return an x-coordinate value snapped to the grid.

```python
# from examples/timer_555_led_blinker/board.py
def horizontal_drop(pad, dx):
    return pad.tox(layout.snap_x(pad.x + dx))
```

#### `layout.snap_y(anchor_or_y)` → `float`
Return a y-coordinate value snapped to the grid.

#### `layout.node(at=None, dx=0, dy=0)` → `BoardAnchor`
Return a geometry anchor without adding any board object. Snaps to grid. Useful for named
intermediate points.

```python
waypoint = layout.node(timer.DISCH.right(2.5))
```

#### `layout.rule(name)` → `float`
Return a board design-rule value by compact name (e.g. `"copper_clearance"`).

---

### Geometry helpers

#### `layout.stack(count=, direction=None, pitch=None, at=None)` → `tuple[BoardAnchor, ...]`
Return `count` evenly-spaced anchors starting from `at` (or the cursor), stepping in
`direction` with `pitch` spacing. Defaults: cursor direction, `layout.unit` pitch.

```python
slots = layout.stack(count=4, direction="Down", pitch=2.54)
```

#### `layout.align(anchors, axis=, target=None)` → `tuple[BoardAnchor, ...]`
Return a tuple of anchors all aligned on one board axis. `axis` is `"x"` or `"y"`.
`target` can be a coordinate, anchor, or `None` (use the first anchor's axis value).

```python
aligned = layout.align([ra.center, rb.center, rc.center], axis="x", target=u1.center)
```

#### `layout.distribute(count=, start=, end=)` → `tuple[BoardAnchor, ...]`
Return `count` evenly-spaced anchors between `start` and `end` (inclusive).

```python
positions = layout.distribute(count=5, start=board.corner("top-left"), end=board.corner("top-right"))
```

#### `layout.mirror(anchors, axis=, about=None)` → `tuple[BoardAnchor, ...]`
Return anchors reflected across the `axis` (`"x"` or `"y"`). `about` sets the reflection
line; defaults to board center.

```python
right_side = layout.mirror([left_cap.center], axis="y")
```

#### `layout.rect(at=None, size=)` → `tuple[BoardAnchor, ...]`
Return four corner anchors of a rectangle. `at` defaults to cursor; `size` is `(w, h)`.
Also available as `layout.rectangle(...)`.

```python
corners = layout.rect(at=u1.center, size=(10.0, 6.0))
```

#### `layout.polygon(vertices)` → `tuple[BoardAnchor, ...]`
Return a tuple of anchors for an arbitrary polygon outline.

```python
outline = layout.polygon([(0, 0), (10, 0), (10, 8), (0, 8)])
```

#### `layout.frame(at=(0,0), direction=None)` → context manager
Temporarily author in a local coordinate frame. Coordinates inside the block are relative
to `at`. Cursor and direction are restored on exit.

```python
with layout.frame(at=u1.center):
    local_cap = layout.two_pad(parts["CDEC"]).at((0, -3.0)).anchor("center").down()
```

---

### Placement

#### `layout.place(component, at=None, orient=None, side="top", locked=False)` → `PlacedBoardComponent`
Place a multi-pin component. `at` defaults to cursor. `orient` is `"Right"`, `"Down"`,
`"Left"`, or `"Up"`. `locked=True` pins the part so it cannot be moved by the cursor.

```python
# from examples/timer_555_led_blinker/board.py
header = layout.place(parts["J1"],
                      at=board.edge("left").center().right(7),
                      orient="right", locked=True)
timer = layout.place(parts["U1"], at=(23.0, 15.0), orient="right", locked=True)
```

`PlacedBoardComponent` supports pin access by name (`timer.VCC`), by number (`timer[1]`),
by pad label (`timer.pad("1")`), `.center`, `.start`, `.end`, and `.pin_anchors()`.

#### `layout.two_pad(component, side="top", locked=False)` → `BoardTwoPadComponent`
Start fluent placement for a two-pad footprint. Chain:

- `.at(point)` — set the anchor point.
- `.anchor(ref)` — choose which local anchor is fixed to `at` (`"start"`, `"end"`,
  `"center"`, pad label, or pin number).
- `.drop(ref)` — choose which local anchor drives the cursor after placement.
- `.right()` / `.left()` / `.up()` / `.down()` — set orientation and materialize.
- `.pad(label)`, `.pin(key)`, `[key]` — access pad/pin anchors after materialization.
- `.center`, `.start`, `.end` — geometry anchors after materialization.

```python
# from examples/timer_555_led_blinker/board.py
ra = (layout.two_pad(parts["RA"])
      .at((36.0, 10.0))
      .anchor("center")
      .right())
rb = (layout.two_pad(parts["RB"])
      .at((36.0, 14.0))
      .anchor("center")
      .right())
ct = (layout.two_pad(parts["CT"])
      .at(timing_junction.down(3.0))
      .down())
```

#### `layout.hold()` → context manager
Save and restore cursor position and direction. Use to place a functional block and return
the cursor to its pre-block state.

```python
# from examples/timer_555_led_blinker/board.py
with layout.hold():
    ra = layout.two_pad(parts["RA"]).at((36.0, 10.0)).anchor("center").right()
    rb = layout.two_pad(parts["RB"]).at((36.0, 14.0)).anchor("center").right()
    timing_junction = layout.snap(rb.end.right(2.0))
    ct = layout.two_pad(parts["CT"]).at(timing_junction.down(3.0)).down()
```

---

### Routing

#### `layout.route(net, layer=, width=0.20, mode="octilinear")` → `BoardRoute`
Start a fluent route builder. `mode` is `"octilinear"` (default), `"orthogonal"`, or
`"direct"`.

**`BoardRoute` chain:**

- `.at(point)` — override start from cursor.
- `.to(point, mode=None)` → `int` — terminate and materialize the track.
- `.through(point, mode=None)` → `BoardRoute` — route via waypoint without materializing.
- `.tox(anchor_or_x)` → `BoardRoute` — route horizontally to an x coordinate.
- `.toy(anchor_or_y)` → `BoardRoute` — route vertically to a y coordinate.
- `.left(distance)` / `.right(distance)` / `.up(distance)` / `.down(distance)` → `BoardRoute`
  — route a relative distance in one direction.

```python
# from examples/timer_555_led_blinker/board.py
layout.route(nets["GND"], layer=front, width=0.30).at(header[2]).to(drop)
layout.route(nets["+5V"], layer=front, width=0.30).at(header[1]).toy(power_rail).tox(cdec.start).to(cdec.start)
layout.route(nets["TIMING"], layer=front).at(timer.TRIG).tox(timing_escape).toy(timer.THRESH).to(timer.THRESH)
```

Use `.through()` to thread a route through an explicit waypoint without a separate
call. (Illustrative, not from the 555 board — that board uses `.tox()`/`.toy()` for its
escapes; `layout.node()` makes the waypoint anchor.)

```python
layout.route(net, layer=front, width=0.25).at(start_pad).through(layout.node((20.0, 3.0))).to(end_pad)
```

#### `layout.connect(start, end, layer=, net=None, width=0.20, through=(), mode="octilinear")` → `int`
Route between two anchors in a single call. `through=(waypoint,)` threads through an
intermediate anchor. `net` is inferred from pad endpoints when `None`.

```python
# from examples/pcb_led_board/main.py
layout.connect(header[1], resistor[1], layer=front, width=0.25, mode="direct")
layout.connect(resistor[2], led.A, layer=front, width=0.25,
               through=(layout.node((20.0, 3.0)),), mode="direct")
```

#### `layout.bundle(pairs, layer=, net=None, width=0.20, mode="octilinear")` → `tuple[int, ...]`
Route multiple independent `(start, end)` anchor pairs as a batch. Returns one track index
per pair.

```python
layout.bundle(
    [(r.start, ic.pin(i)) for i, r in enumerate(pull_ups)],
    layer=front, width=0.20,
)
```

#### `layout.via(net, at=None, start_layer=, end_layer=, drill=None, annular=None)` → `int`
Add a via at `at` (defaults to cursor) and move the cursor there. Returns the via index.

```python
# from examples/timer_555_led_blinker/board.py
layout.via(nets["GND"], at=drop, start_layer=front, end_layer=back)
```

#### `layout.stitch(net, at=, start_layer=, end_layer=, drill=None, annular=None)` → `tuple[int, ...]`
Add a deterministic set of vias for one net at multiple anchors. `at` accepts any
iterable of anchors. Returns a tuple of via indices.

```python
layout.stitch(nets["GND"], at=gnd_anchor_list, start_layer=front, end_layer=back)
```

#### `layout.fanout(anchors, layer=, direction=, distance=, net=None, width=0.20, via_layers=None, drill=None, annular=None)` → `tuple[BoardFanout, ...]`
Route pads outward by `distance` in `direction`, optionally dropping vias at the endpoints.
`via_layers=(start, end)` adds vias. Returns `BoardFanout` objects with `.start`, `.end`,
`.via` attributes.

```python
layout.fanout(ic.pin_anchors(), layer=front, direction="Down",
              distance=1.0, via_layers=(front, back))
```

---

### Fill geometry

#### `layout.zone(layers=, net=None, outline=None, at=None, size=None, fill="solid", priority=0)` → `int`
Add a copper fill zone. Define the outline with `outline=` (anchor iterable) or with
`at=` + `size=` for a rectangle. Returns the zone index.

```python
layout.zone(layers=(back,), net=nets["GND"],
            at=(0.0, 0.0), size=(48.0, 32.0))
```

#### `layout.keepout(layers=, restrictions=, outline=None, at=None, size=None)` → `int`
Add a keepout region. `restrictions` is an iterable of restriction strings (e.g.
`("tracks", "vias")`). Define the area with `outline=` or `at=` + `size=`.

```python
layout.keepout(layers=(front, back), restrictions=("tracks", "vias"),
               at=hole.center, size=(5.0, 5.0))
```

#### `layout.text(text, at=, layer=, rotation=0.0, size=1.0, locked=False)` → `int`
Add board text at a layout anchor. Returns the text index.

```python
layout.text("GND", at=timer.GND.down(1.5), layer=silk, size=0.7)
```

---

## BoardAnchor methods

Every placed pad and computed point returns a `BoardAnchor`. Anchors are composable:

| Method | Returns |
|---|---|
| `.left(distance)` | New anchor moved left |
| `.right(distance)` | New anchor moved right |
| `.up(distance)` | New anchor moved up |
| `.down(distance)` | New anchor moved down |
| `.offset(dx=0, dy=0)` | New anchor at explicit offset |
| `.tox(anchor_or_x)` | New anchor at same y, target x |
| `.toy(anchor_or_y)` | New anchor at same x, target y |
| `.x`, `.y` | Float coordinate values |
| `.point` | `(x, y)` tuple |

```python
# from examples/timer_555_led_blinker/board.py
disch_escape = layout.snap(timer.DISCH.right(2.5))
disch_bus    = layout.snap(ra.end.right(1.6).down(2.0))
```
