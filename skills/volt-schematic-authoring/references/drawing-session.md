# Drawing Session Reference

**Source:** `python/volt/schematic.py` — `SchematicDrawing` class.
**Canonical example:** `examples/timer_555_led_blinker/schematic.py`.

A `SchematicDrawing` is obtained by calling `sheet.drawing(at=, unit=)` as a context
manager. The cursor state (position, direction, unit) is local to one session.

```python
with sheet.drawing(at=(140, 80), unit=20) as drawing:
    ...
```

---

## Constructor Parameters

| Parameter | Default | Meaning |
|---|---|---|
| `at` | `(0, 0)` | Initial cursor position. Accepts a coordinate tuple, `SchematicAnchor`, or `SchematicPort`. |
| `direction` | `"Right"` | Initial direction: `"Right"`, `"Down"`, `"Left"`, or `"Up"`. |
| `unit` | `20` | Drawing distance used by `.right()`, `.down()`, and default element length. |

---

## Cursor Properties

| Property | Returns | Purpose |
|---|---|---|
| `drawing.here` | `SchematicAnchor` | Current cursor anchor. |
| `drawing.direction` | `str` | Current orientation string. |
| `drawing.unit` | `float` | Drawing unit distance. |

---

## Element Placement

### `drawing.two_terminal(component, *, symbol=, variant=, reference_label=)`

Start fluent placement for a two-terminal component. Returns a deferred
`SchematicTwoTerminalElement` which is not materialized until a property is accessed.
The cursor advances to the drop endpoint after materialization.

From `examples/timer_555_led_blinker/schematic.py`:
```python
ra = (
    drawing.two_terminal(parts["RA"])
    .at(timer.DISCH.up(drawing.unit))
    .to(timer.DISCH)
    .label_ref(loc="left", offset=8)
    .label_value(loc="left", offset=24)
)
```

See `two-terminal-builder.md` for the full chain.

### `drawing.place(component, *, at=, orient=, symbol=, variant=, reference_label=)`

Place a multi-pin or IC component at the current cursor (or an explicit `at=` anchor).
Returns a `PlacedSchematicElement`. Named pin anchors are exposed as attributes.

From `examples/timer_555_led_blinker/schematic.py`:
```python
timer = (
    drawing.place(parts["U1"])
    .label_ref(loc="top", offset=4)
    .label_value(loc="bottom", offset=14, align="end")
)
header = drawing.place(parts["J1"], at=(72, 84), orient="Right").label_ref(loc="left")
```

### `drawing.node(at=, *, dx=, dy=)`

Return a reusable `SchematicAnchor` at a point without adding any schematic object. Useful
for holding a geometry reference to use later.

---

## Wire Authoring

### `drawing.connect(*args, net=, shape=, k=)`

Project a wire between two anchors. Infers the net when both endpoints are pin anchors
on the same logical net. Pass `net=` for coordinate or offset anchors.

```python
drawing.connect(timer.THRESH, timer.TRIG, shape="-")
drawing.connect(timer.RESET, timer.VCC, shape="-").dot()
```

Both quoted from `examples/timer_555_led_blinker/schematic.py`.

### `drawing.wire(net)` / `drawing.line(net)`

Start a `SchematicWireBuilder` from the current cursor. Chain `.from_()` / `.at()` /
`.via()` / `.to()` / `.tox()` / `.toy()` / `.right()` / `.left()` / `.up()` / `.down()`
then call `.direct()` or `.orthogonal()`.

From `examples/timer_555_led_blinker/schematic.py`:
```python
drawing.wire(nets["GND"]).at(timing_cap.end).tox(ground.pin).direct()
drawing.wire(nets["GND"]).at(control_cap.end).tox(ground.pin).direct()
drawing.wire(nets["GND"]).at(led.end).tox(ground.pin).direct()
```

`line()` is an alias for `wire()`.

### `drawing.ortho_lines(entries, *, shape=, k=)`

Place multiple wire runs in one call. Each entry is `(start, end)` (net inferred from
pin anchors) or `(net, start, end)` (explicit net handle for non-pin anchors).

From `examples/schematic_sugar/timer_555_led_blinker.py`:
```python
drawing.ortho_lines(
    (
        (nets["+5V"], source[1], vcc),
        (nets["GND"], source[2], ground),
        (nets["TIMING"], timer.THRESH, timing_cap.start),
        (nets["TIMING"], timer.TRIG, timing_cap.start),
    ),
    shape="-|",
    k=-18,
)
```

---

## Power, Ground, Labels, Stubs, Tags, Ports

All helpers below visualize existing nets. Pass `net=` whenever the anchor is a
coordinate, offset, or label anchor that does not prove the net by itself.

### `drawing.power(name, *, at=, net=, orient=)`

Place a power marker symbol on an existing net. `orient` defaults to `"Up"`.

```python
drawing.power("+5V", net=nets["+5V"], at=timer.VCC.up(18), orient="Up")
```
From `examples/schematic_sugar/timer_555_led_blinker.py`.

### `drawing.power_stub(name, *, at=, net=, side=, length=, orient=)`

Draw a short wire from an anchor to a power marker. `side` defaults to `"Up"`.

```python
drawing.power_stub("+5V", at=timer.VCC)
drawing.power_stub("+5V", at=header[1], net=nets["+5V"], side="Up", length=20)
drawing.power_stub("+5V", at=ra.start)
drawing.power_stub("+5V", at=decoupling_cap.start)
```
All from `examples/timer_555_led_blinker/schematic.py`.

### `drawing.ground(name=, *, at=, net=, orient=)`

Place a ground marker symbol. `orient` defaults to `"Down"`. `name` may be `None` (net
name used).

```python
ground = drawing.ground("GND", at=timer.GND, orient="Down")
```
From `examples/timer_555_led_blinker/schematic.py`.

### `drawing.ground_stub(name=, *, at=, net=, side=, length=, orient=)`

Draw a short wire from an anchor to a ground marker. `side` defaults to `"Down"`.

```python
drawing.ground_stub("GND", at=header[2], net=nets["GND"], side="Down", length=20)
drawing.ground_stub("GND", at=decoupling_cap.end)
```
From `examples/timer_555_led_blinker/schematic.py`.

### `drawing.net_label(name_or_net, *, at=, orient=, label=)`

Place a net label at an anchor. Accepts a `Net` handle or a net name string.

```python
drawing.net_label(nets["LED_A"], at=resistor.end.up(10))
```
From `examples/schematic_sugar/compact_led.py`.

### `drawing.local_label(name_or_net, *, at=, side=, offset=, orient=)`

Place a short-offset local net label near an anchor. `side` defaults to `"Right"`,
`offset` defaults to `2`.

```python
drawing.local_label(
    nets["TIMING"],
    at=timing_cap.start,
    side="Left",
    offset=34,
    orient="Right",
)
```
From `examples/timer_555_led_blinker/schematic.py`.

```python
drawing.local_label(nets["TIMING"], at=timing_cap.start, side="Left", offset=4)
drawing.local_label(nets["OUT"], at=led_resistor.start, side="Up", offset=5)
```
From `examples/schematic_sugar/timer_555_led_blinker.py`.

### `drawing.signal_stub(name_or_net, *, at=, side=, length=, label_gap=, orient=, label=)`

Draw a short stub wire ending in a local net label. Infers `side` from the anchor
orientation when not given. `length` defaults to `8`, `label_gap` to `2`.

### `drawing.signal_tag(name_or_net, *, at=, side=, length=, label=, kind=, orient=)`

Draw a short stub wire ending in a sheet-port-style tag. `kind` defaults to
`"Bidirectional"`.

### `drawing.signal_tags(items, *, at=, side=, pitch=, length=, kind=, orient=)`

Place a pitched stack of signal tags. `pitch` defaults to `8`.

### `drawing.signal_stubs(items, *, at=, side=, pitch=, length=, label_gap=, orient=)`

Place a pitched stack of signal stubs.

### `drawing.terminal(name_or_net=, *, at=, net=, kind=, orient=)`

Place a generic terminal marker. `kind` is `"Power"` or `"Ground"`.

### `drawing.terminal_stub(name_or_net=, *, at=, net=, kind=, side=, length=, orient=)`

Draw a short wire from an anchor to a terminal marker.

### `drawing.junction(net, *, at=)`

Place an explicit junction dot on a logical net at the cursor or an explicit anchor.

### `drawing.no_connect(anchor, *, orient=, offset=, reason=)`

Place a no-connect marker on a placed pin anchor. `anchor` must be a `SchematicPinAnchor`.

### `drawing.sheet_port(name, *, at=, net=, kind=, orient=)`

Place a sheet port on an existing or name-resolved logical net. `kind` defaults to
`"Bidirectional"`.

### `drawing.off_page(name, *, at=, net=, orient=)`

Place an off-page connector on an existing or name-resolved logical net.

---

## Cursor Movement

### `drawing.move(*, dx=, dy=)`

Move the cursor by a relative offset.

### `drawing.move_from(anchor, *, dx=, dy=, direction=)`

Jump the cursor to an anchor plus an optional offset, optionally changing direction.

```python
drawing.move_from(timer.DISCH.left(36).up(22))
```
From `examples/schematic_sugar/timer_555_led_blinker.py`.

### `drawing.push()` / `drawing.pop()`

Save and restore cursor position and direction without a context manager.

### `drawing.hold()`

Context manager that saves cursor state on entry and restores it on exit, regardless of
how the cursor moves inside the block.

```python
with drawing.hold():
    drawing.move_from(timer.DISCH.left(36).up(22))
    ra = drawing.two_terminal(parts["RA"]).down().label_ref().label_value()
    rb = drawing.two_terminal(parts["RB"]).at(timer.DISCH.left(36)).down().label_value(loc="left")
```
From `examples/schematic_sugar/timer_555_led_blinker.py`.

### `drawing.stack(*, count, direction=, pitch=, at=)`

Return `count` evenly spaced `SchematicAnchor` values from the cursor. Defaults to the
current direction and unit as pitch.

### `drawing.frame(at=, *, direction=)`

Context manager that sets a local coordinate frame. Inside the block, coordinate tuples
are interpreted relative to `at`. Cursor and stack state are restored on exit.
