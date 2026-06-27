# Low-Level Schematic API Reference

**Source:** `docs/python-api.md` section "Schematic Placement"; `python/volt/schematic.py`
— `Schematic` class (the object returned by `design.schematic()`).

This is the **fallback** surface. Use the `sheet.drawing()` session described in
`drawing-session.md` for new work. The low-level API remains useful when you need precise
absolute positioning for a complex IC, want to load or inspect existing schematic JSON, or
are working outside a drawing context.

---

## `design.schematic(name, ...)` — Sheet Setup

```python
sheet = design.schematic(
    "Main",
    size=(340, 240),
    orientation="landscape",
    title="My Board",
    number=1,
    page_count=1,
    revision="A",
    date="2026-05-19",
    project="My Project",
    file="schematic.py",
    margins=(10, 8, 10, 8),
    coordinate_zones=(6, 4),
    grid={"spacing": 5, "visible": False},
)
```

`design.schematic()` is idempotent by name. Calling it a second time with metadata updates
the title block.

| Argument | Type | Default | Meaning |
|---|---|---|---|
| `name` | `str` | required | Sheet name, unique within the design. |
| `size` | `str \| tuple[float, float] \| dict \| None` | `None` | Sheet size. A tuple is `(width, height)` in mm. |
| `orientation` | `str \| None` | `None` | `"landscape"` or `"portrait"`. |
| `title` | `str \| None` | `None` | Title block title string. |
| `number` | `str \| int \| None` | `None` | Sheet number. |
| `page_count` | `str \| int \| None` | `None` | Total page count. |
| `revision` | `str \| None` | `None` | Revision identifier. |
| `date` | `str \| None` | `None` | Date string for the title block. |
| `project` | `str \| None` | `None` | Project name for the title block. |
| `file` | `str \| None` | `None` | Source file path for the title block. |
| `margins` | `float \| tuple[float, float, float, float] \| dict \| None` | `None` | Sheet margins. A tuple is `(top, right, bottom, left)`. |
| `coordinate_zones` | `tuple[int, int] \| dict \| None` | `None` | Number of coordinate zone columns and rows. |
| `grid` | `float \| dict \| None` | `None` | Grid spacing (plain float) or dict with `"spacing"` and `"visible"`. |

---

## `sch.place(component, *, at, orient=, symbol=, variant=, reference_label=)`

Place a component symbol at an explicit coordinate:

```python
r_sym = sch.place(r1, at=(40, 20), orient="Right", symbol="resistor")
d_sym = sch.place(d1, at=(110, 30), symbol="led")
```

From `docs/python-api.md` §"Schematic Placement".

| Argument | Type | Default | Meaning |
|---|---|---|---|
| `component` | `Component` | required | The component to place. Must belong to this design. |
| `at` | `tuple[float, float]` | required | Absolute sheet coordinate in mm. |
| `orient` | `str` | `"Right"` | Orientation: `"Right"`, `"Down"`, `"Left"`, `"Up"`, plus mirror forms. |
| `symbol` | `str \| SchematicSymbolSpec \| None` | `None` | Symbol name or spec. Falls back to the component's default symbol. |
| `variant` | `str` | `"default"` | Symbol variant name. |
| `reference_label` | `str \| None` | `None` | If given, adds a reference field label at the symbol origin. |

Returns a `SchematicSymbol` handle. Use `symbol.pin(key)` or `symbol.pin_anchor(number)`
to get pin coordinates. Pin handles expose `.left(distance)`, `.right(distance)`,
`.up(distance)`, `.down(distance)` for nearby label and port placement.

---

## `sch.wire(net, points=)` — Wire Builder or Direct Wire

```python
# Fluent builder
sch.wire(led_a).from_(r_sym.pin(2)).to(d_sym.pin("A")).orthogonal()

# Explicit point list
sch.wire(led_a, [(40, 20), (70, 20), (70, 30)])
```

From `docs/python-api.md` §"Schematic Placement".

When `points` is `None` (the default), returns a `SchematicWireBuilder`. When `points` is
provided, creates the wire immediately and returns a `SchematicWire`.

### `SchematicWireBuilder` chain

| Method | Arg | Purpose |
|---|---|---|
| `.from_(point)` / `.at(point)` | anchor or coordinate | Start the wire run. |
| `.via(point)` | anchor or coordinate | Add an explicit intermediate point. |
| `.to(point, *, shape=, k=)` | anchor or coordinate | Append the endpoint (or next point). |
| `.tox(anchor_or_x)` | anchor or x value | Append a horizontal segment to a target x. |
| `.toy(anchor_or_y)` | anchor or y value | Append a vertical segment to a target y. |
| `.right(length)` | float | Append a rightward segment. |
| `.left(length)` | float | Append a leftward segment. |
| `.up(length)` | float | Append an upward segment. |
| `.down(length)` | float | Append a downward segment. |
| `.dot()` | — | Request a junction dot at the final endpoint. |
| `.idot()` | — | Request a junction dot at the initial endpoint. |
| `.direct()` | — | Persist the run without inserting a bend. Returns `SchematicWire`. |
| `.orthogonal()` | — | Persist the run, inserting one bend for a diagonal route. Returns `SchematicWire`. |
| `.shape(shape, *, k=)` | str | Persist a shaped route. Returns `SchematicWire`. |

Example from `docs/python-api.md` §"Schematic Placement":
```python
sch.wire(vcc).from_(vcc_port).to(r_sym.pin(1)).orthogonal()
sch.wire(led_a).from_(r_sym.pin(2)).via(r_sym.pin(2).right(30)).to(d_sym.pin("A")).orthogonal()
sch.wire(gnd).from_(d_sym.pin("K")).to(gnd_port).orthogonal()
```

---

## `sch.connect(start, end, *, net=, shape=, k=)` — Shorthand Wire

Creates an orthogonal wire in one call. Net is inferred from pin anchors or supplied
explicitly:

```python
sch.connect(regulator_out, output_cap.start)
sch.connect(gnd_pin, ground_port, net=gnd, shape="|-", k=10)
```

---

## `sch.ortho_lines(entries, *, shape=, k=)`

Place multiple orthogonal wire runs. Each entry is `(start, end)` or `(net, start, end)`.

---

## Power, Ground, Label, Junction, Port

All helpers require that the referenced net already exists in the logical circuit.

```python
# From docs/python-api.md §"Schematic Placement"
vcc_port = sch.power("VCC", net=vcc, at=r_sym.pin(1).left(20))
gnd_port = sch.ground(net=gnd, at=d_sym.pin("K").down(30))
sch.label(led_a, at=r_sym.pin(2).right(8), orient="Left")
sch.junction(led_a, at=r_sym.pin(2).right(35))
```

| Method | Signature | Purpose |
|---|---|---|
| `sch.power(name, *, at, net=, orient=)` | Returns `SchematicPort` | Power marker at explicit anchor. |
| `sch.power_stub(name, *, at=, net=, side=, length=, orient=)` | Returns `SchematicTerminalStub` | Wire + power marker from anchor. |
| `sch.ground(name=, *, at, net=, orient=)` | Returns `SchematicPort` | Ground marker at explicit anchor. |
| `sch.ground_stub(name=, *, at=, net=, side=, length=, orient=)` | Returns `SchematicTerminalStub` | Wire + ground marker from anchor. |
| `sch.net_label(name_or_net, *, at, orient=, label=)` | Returns `SchematicNetLabel` | Net label at anchor. |
| `sch.local_label(name_or_net, *, at, side=, offset=, orient=)` | Returns `SchematicNetLabel` | Short-offset local net label. |
| `sch.signal_stub(name_or_net, *, at, side=, length=, label_gap=, orient=, label=)` | Returns `SchematicSignalStub` | Stub wire + local label. |
| `sch.signal_tag(name_or_net, *, at, side=, length=, label=, kind=, orient=)` | Returns `SchematicSignalTag` | Stub wire + port-style tag. |
| `sch.junction(net, *, at)` | Returns `SchematicJunction` | Junction dot. |
| `sch.no_connect(anchor, *, orient=, offset=, reason=)` | Returns `SchematicNoConnect` | No-connect marker on a pin anchor. |
| `sch.sheet_port(name, *, at, net=, kind=, orient=)` | Returns `SchematicPort` | Sheet port (hierarchical boundary). |
| `sch.off_page(name, *, at, net=, orient=)` | Returns `SchematicPort` | Off-page connector for same-level continuation. |

---

## `sch.region(name, *, x, y, w, h, title=, style=)` — Named Regions

Declare a named rectangular region on the sheet for functional grouping documentation:

```python
sheet.region("timing", x=100, y=40, w=80, h=60, title="Timing Network")
```

---

## Serialization

```python
schematic_json = sch.to_json()
sch.write_json("led.schematic.volt.json")

schematic_svg = sch.to_svg()
sch.write_svg("led.svg")

# Reload and validate
loaded = design.load_schematic_json(schematic_json)
design.load_schematic(path)
```

The schematic JSON is the source of truth. SVG is a rendered artifact.
