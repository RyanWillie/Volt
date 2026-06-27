# Two-Terminal Builder Reference

**Source:** `python/volt/schematic.py` — `SchematicTwoTerminalElement` class.
**Canonical examples:** `examples/timer_555_led_blinker/schematic.py`,
`examples/schematic_sugar/timer_555_led_blinker.py`, `examples/schematic_sugar/compact_led.py`.

`SchematicTwoTerminalElement` is returned by `drawing.two_terminal(component)`. The element
is **deferred**: no symbol is placed until a property or materializing method is accessed.
The drawing cursor advances to the drop endpoint each time the geometry changes.

A two-terminal component must have exactly two pins. The builder validates this on
construction.

---

## Positioning

### `.at(point)`

Set the anchor point. Accepts a coordinate tuple, `SchematicAnchor`, or `SchematicPort`.
Overrides the cursor position.

```python
drawing.two_terminal(parts["RA"]).at(timer.DISCH.up(drawing.unit))
```
From `examples/timer_555_led_blinker/schematic.py`.

### `.anchor(ref)`

Choose which terminal is fixed to the `at` point. `ref` is `"start"`, `"end"`,
`"center"`, or a pin number. Defaults to `"start"`.

### `.drop(ref)`

Choose which terminal advances the drawing cursor after placement. `ref` is `"start"`,
`"end"`, `"center"`, or a pin number. Defaults to `"end"`.

### `.to(end)`

Place the element from its current anchor to an aligned endpoint. The start and end
anchors must be horizontally or vertically aligned; the orientation is inferred from the
direction between them.

```python
drawing.two_terminal(parts["RA"]).at(timer.DISCH.up(drawing.unit)).to(timer.DISCH)
```
From `examples/timer_555_led_blinker/schematic.py`.

### `.tox(anchor_or_x)`

Place the element horizontally to a target x coordinate (or the x coordinate of an
anchor). The element starts at the current `at` point.

```python
drawing.two_terminal(parts["RB"]).at(timer.DISCH).tox(timer.THRESH)
```
Illustrative; compare with the `toy` form in `examples/timer_555_led_blinker/schematic.py`:

```python
rb = (
    drawing.two_terminal(parts["RB"])
    .at(timer.DISCH)
    .toy(timer.THRESH)
    ...
)
```

### `.toy(anchor_or_y)`

Place the element vertically to a target y coordinate (or the y coordinate of an anchor).

```python
rb = (
    drawing.two_terminal(parts["RB"])
    .at(timer.DISCH)
    .toy(timer.THRESH)
    .label_ref(loc="left", offset=8)
    .label_value(loc="left", offset=22)
    .idot()
    .dot()
)
timing_cap = (
    drawing.two_terminal(parts["CT"])
    .at(timer.TRIG)
    .toy(ground.pin)
    .label_ref(loc="left", offset=8)
    .label_value(loc="left", offset=22)
    .idot()
    .dot()
)
```
Both from `examples/timer_555_led_blinker/schematic.py`.

### `.between(start, end, *, anchor=, drop=)`

Place the element between two aligned anchors. `anchor` and `drop` default to `"start"`
and `"end"`. Equivalent to calling `.at(start).to(end)` with explicit anchor/drop
control.

### `.endpoints(start, end, *, anchor=, drop=)`

Alias for `.between()`.

---

## Orientation and Sizing

### `.right(length=)`, `.left(length=)`, `.up(length=)`, `.down(length=)`

Set orientation. The optional `length` is in drawing units (multiplied by `drawing.unit`).
When no length is given, the element uses the current length setting.

```python
ra = drawing.two_terminal(parts["RA"]).down().label_ref(loc="left").label_value(loc="right")
led_resistor = drawing.two_terminal(parts["RLED"]).at(timer.OUT).right().label_value(loc="top", offset=8)
```
Both from `examples/schematic_sugar/timer_555_led_blinker.py`.

### `.length(value)`

Set the element length in drawing units (multiplied by `drawing.unit`). Must be positive.

---

## Presentation Modifiers

### `.reverse()`

Swap the generated symbol's start and end pins. The logical netlist is unchanged; only
the symbol orientation changes. Useful for LEDs and diodes placed in a specific visual
direction.

```python
led = (
    drawing.two_terminal(parts["DLED"])
    .reverse()
    .toy(ground.pin)
    .label_ref(loc="right", offset=14)
)
```
From `examples/timer_555_led_blinker/schematic.py`.

```python
led = drawing.two_terminal(parts["D1"]).right().reverse().label_ref()
```
From `examples/schematic_sugar/compact_led.py`.

### `.flip()`

Flip the generated symbol across the placement axis. Toggles on each call.

---

## Labels

### `.label(text, *, loc=, name=, offset=, ofst=, orient=)`

Materialize the element and add a text field near it. `name` defaults to `"label"`.
`offset` and `ofst` are synonyms.

### `.label_ref(*, loc=, offset=, ofst=, orient=)`

Materialize the element and emit its reference-designator field.

```python
drawing.two_terminal(parts["RA"]).at(...).to(...).label_ref(loc="left", offset=8)
drawing.place(parts["U1"]).label_ref(loc="top", offset=4)
```
Both from `examples/timer_555_led_blinker/schematic.py`.

### `.label_value(*, loc=, offset=, ofst=, orient=)`

Materialize the element and emit its value field.

```python
drawing.two_terminal(parts["RA"]).at(...).to(...).label_value(loc="left", offset=24)
drawing.two_terminal(parts["RB"]).at(...).toy(...).label_value(loc="left", offset=22)
```
Both from `examples/timer_555_led_blinker/schematic.py`.

---

## Junction Dots

### `.dot(*, net=)`

Materialize the element and add a junction dot at the **end** terminal.

### `.idot(*, net=)`

Materialize the element and add a junction dot at the **start** terminal.

```python
rb = (
    drawing.two_terminal(parts["RB"])
    .at(timer.DISCH)
    .toy(timer.THRESH)
    ...
    .idot()
    .dot()
)
```
From `examples/timer_555_led_blinker/schematic.py`. `idot()` marks the start (DISCH node);
`dot()` marks the end (THRESH node). Both join pre-existing T-junctions in the schematic.

---

## Accessing Terminals After Placement

These properties materialize the element if it has not been materialized yet.

| Property / Method | Returns | Purpose |
|---|---|---|
| `.start` | `SchematicPinAnchor` | The start pin anchor. |
| `.end` | `SchematicPinAnchor` | The end pin anchor. |
| `.center` | `SchematicAnchor` | The geometric center anchor. |
| `.pin(key)` | `SchematicPinAnchor` | Pin anchor by name or number. |
| `.pins(name)` | `tuple[SchematicPinAnchor, ...]` | All pins with the given name. |
| `.pin_anchors()` | `tuple[SchematicPinAnchor, ...]` | All pin anchors. |
| `.pin_anchor(number)` | `tuple[float, float]` | Raw coordinate for a pin number. |
| `element[key]` | `SchematicPinAnchor` | Pin anchor by name or number (index syntax). |
| Named attribute | `SchematicPinAnchor` | Unique pin name as attribute (e.g. `ra.start`). |

```python
drawing.power_stub("+5V", at=ra.start)
drawing.wire(nets["GND"]).at(timing_cap.end).tox(ground.pin).direct()
```
Both from `examples/timer_555_led_blinker/schematic.py`.

---

## Other Properties

| Property | Returns | Purpose |
|---|---|---|
| `.index` | `int` | Kernel index after materialization. |
| `.component` | `Component` | The component being placed. |
| `.orientation` | `str` | The placed symbol orientation after materialization. |

---

## Fluent Return Types

All positioning and presentation modifier methods return `self` (`SchematicTwoTerminalElement`)
until a materializing method or property is accessed. Once materialized:

- `.label()`, `.label_ref()`, `.label_value()` return `PlacedSchematicElement`.
- `.dot()`, `.idot()` return `PlacedSchematicElement`.
- `.start`, `.end`, `.center`, `.pin()`, `.pins()`, `.pin_anchors()`, `.index`,
  `.component`, `.orientation` return their respective types.

Materializing calls cannot be chained back to the builder. Place all `.at()`, `.to()`,
`.toy()`, `.reverse()`, `.flip()` calls before any materializing call.
