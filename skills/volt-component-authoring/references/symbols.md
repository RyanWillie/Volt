# Schematic Symbol Cookbook

Source: `examples/timer_555_led_blinker/components.py`

---

## Key type: `volt.SchematicSymbolSpec`

Defined in `python/volt/library.py`. Pass an instance (or iterable of instances for multi-unit parts) as `schematic_symbol=` to `Design.define_component()`.

---

## IC-style block symbol: `.ic()`

**Signature** (`python/volt/library.py` line 432):

```python
SchematicSymbolSpec.ic(
    name: str,
    *,
    pins: Iterable[SchematicBlockPinSpec],
    width: float | None = None,
    height: float | None = None,
    lead_length: float = 6,
    pin_pitch: float = 10,
    pin_label_offset: float = 2,
    side_layouts: Iterable[SchematicBlockSideLayoutSpec] = (),
    center_label: str | None = None,
    bottom_label: str | None = None,
    pin_labels: bool = True,
    pin_numbers: bool = False,
    pin_number_offset: float = 2,
    variant: str = "default",
) -> SchematicSymbolSpec
```

`.ic()` is equivalent to `.block()` — both build a rectangular block symbol. Use `.ic()` for ICs by convention.

### Pin placement: `.ic_pin()` (alias for `.block_pin()`)

**Signature** (`python/volt/library.py` line 358):

```python
SchematicSymbolSpec.ic_pin(
    name: str,
    number: int | str,
    *,
    side: str,
    slot: int | None = None,
    label: str | None = None,
) -> SchematicBlockPinSpec
```

`side` values: `"left"`, `"right"`, `"top"`, `"bottom"`.
`slot` is the pin's position index along that side (1-based, lower slot = closer to top/left).
`label` overrides the display label; omit to use `name` as the label.

### TIMER_SYMBOL from the 555 example

Quoted verbatim from `examples/timer_555_led_blinker/components.py`:

```python
TIMER_SYMBOL = volt.SchematicSymbolSpec.ic(
    "volt.examples.timer_555_led_blinker:NE555",
    pins=(
        volt.SchematicSymbolSpec.ic_pin("DISCH",  7, side="left",   slot=1, label="DIS"),
        volt.SchematicSymbolSpec.ic_pin("THRESH", 6, side="left",   slot=2, label="THR"),
        volt.SchematicSymbolSpec.ic_pin("TRIG",   2, side="left",   slot=3, label="TRG"),
        volt.SchematicSymbolSpec.ic_pin("OUT",    3, side="right",  slot=2),
        volt.SchematicSymbolSpec.ic_pin("CTRL",   5, side="right",  slot=3, label="CTL"),
        volt.SchematicSymbolSpec.ic_pin("RESET",  4, side="top",    slot=2, label="RST"),
        volt.SchematicSymbolSpec.ic_pin("VCC",    8, side="top",    slot=4, label="Vcc"),
        volt.SchematicSymbolSpec.ic_pin("GND",    1, side="bottom", slot=3),
    ),
    center_label="555",
    pin_numbers=True,
)
```

Pin `name` must match the `PinSpec` name used in `define_component`. `number` is the physical pin number displayed when `pin_numbers=True`.

---

## Custom primitive symbol: `SchematicSymbolSpec(...)`

Use the direct constructor for non-rectangular symbols. Pass `pins` and `primitives` explicitly.

**Constructor** (dataclass, `python/volt/library.py`):

```python
SchematicSymbolSpec(
    name: str,
    pins: Iterable[SchematicSymbolPinSpec],
    primitives: Iterable[dict],
    variant: str = "default",
)
```

### Pin anchor: `.pin()`

**Signature** (`python/volt/library.py` line 336):

```python
SchematicSymbolSpec.pin(
    name: str,
    number: int | str,
    at: tuple[float, float],
    orientation: str = "Right",
) -> SchematicSymbolPinSpec
```

`at` is the pin connection point in symbol-local coordinates. `orientation` controls which direction the wire leaves the symbol: `"Right"`, `"Left"`, `"Up"`, `"Down"`.

### Primitives

All primitives are plain dicts created by static methods:

**`.line(start, end, *, role=None)`** (`library.py` line 468):
```python
volt.SchematicSymbolSpec.line((0, 0), (8, 0))
```

**`.rectangle(first_corner, second_corner)`** (`library.py` line 495):
```python
volt.SchematicSymbolSpec.rectangle((8, -4), (22, 12))
```

**`.text(text, at, orientation="Right", *, align="middle", baseline="baseline", font_size=None)`** (`library.py` line 525):
```python
volt.SchematicSymbolSpec.text("J", (15, -10))
```

### SUPPLY_SYMBOL from the 555 example

Quoted verbatim from `examples/timer_555_led_blinker/components.py`:

```python
SUPPLY_SYMBOL = volt.SchematicSymbolSpec(
    "volt.examples.timer_555_led_blinker:ExternalSupply",
    pins=(
        volt.SchematicSymbolSpec.pin("1", 1, (0, 0), "Left"),
        volt.SchematicSymbolSpec.pin("2", 2, (0, 8), "Left"),
    ),
    primitives=(
        volt.SchematicSymbolSpec.rectangle((8, -4), (22, 12)),
        volt.SchematicSymbolSpec.text("J", (15, -10)),
        volt.SchematicSymbolSpec.line((0, 0), (8, 0)),
        volt.SchematicSymbolSpec.line((0, 8), (8, 8)),
    ),
)
```

This supply connector has two pin anchors at `(0, 0)` and `(0, 8)`, a rectangle body, a text label, and two horizontal lead lines connecting the pin points to the body edge.

---

## Attaching the symbol to a component definition

Pass the symbol as `schematic_symbol=` to `define_component()`:

```python
design.define_component(
    "NE555",
    pins=[...],
    schematic_symbol=TIMER_SYMBOL,
)
```

For multi-unit parts, pass an iterable of `SchematicSymbolSpec` objects (one per unit/variant).
