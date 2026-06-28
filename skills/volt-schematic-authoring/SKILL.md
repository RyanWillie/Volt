---
name: volt-schematic-authoring
description: Author, review, or polish Volt-native schematics over existing logical connectivity. Use when working on schematic placement, symbols, wires, labels, ports, regions, visual readability, rendered SVG inspection, schematic diagnostics, or schematic project tests.
---

# Volt Schematic Authoring

First read `../shared-volt-architecture.md`.

## Precondition: Connectivity Already Exists

A schematic **presents** logical connectivity; it does not create it. Before writing a single
placement call, confirm:

- All components are instantiated and all pins are connected to nets in the logical circuit.
- Intentional no-connect intent is marked on the component (not on the schematic).
- No wire you are about to draw should need to join two different logical nets. If it would,
  fix the circuit first.

If `design.validate()` has errors, resolve them in the logical layer before authoring the
schematic. The schematic projection layer cannot connect pins, create nets, merge net names,
or correct logical circuit errors.

## Sheet Setup

Create a named sheet from the design:

```python
sheet = design.schematic(
    "555 LED Blinker",
    size=(340, 240),
    orientation="landscape",
    title="555 LED Blinker Reference Schematic",
    number=1,
    page_count=1,
    revision="A",
    date="2026-05-19",
    project="Volt 555 LED Blinker",
    file=SHEET_FILE,
    margins=(10, 8, 10, 8),
    coordinate_zones=(6, 4),
    grid={"spacing": 5, "visible": False},
)
```

`design.schematic()` is idempotent by name. Providing metadata on subsequent calls updates
the title block. All arguments except `name` are optional.

See `references/low-level-api.md` for the full `design.schematic()` argument table.

## Preferred Path: The `drawing()` Session

The preferred way to author a schematic is the SchemDraw-style cursor API accessed through
`sheet.drawing()`. Use it as a context manager so the cursor flushes pending placements on
exit:

```python
with sheet.drawing(at=(140, 80), unit=20) as drawing:
    ...
```

`at=` sets the initial cursor position. `unit=` sets the drawing distance used by
`.right()`, `.down()`, `.up()`, `.left()`, and the default element length. Inside the
block, `drawing.here` is the current sheet point; `drawing.direction` is the current
orientation (`"Right"`, `"Down"`, `"Left"`, or `"Up"`).

### Two-Terminal Fluent Builder

For resistors, capacitors, inductors, diodes, LEDs, and any other exactly-two-pin component,
use `drawing.two_terminal(component)` and chain the positioning methods:

```python
ra = (
    drawing.two_terminal(parts["RA"])
    .at(timer.DISCH.up(drawing.unit))
    .to(timer.DISCH)
    .label_ref(loc="left", offset=8)
    .label_value(loc="left", offset=24)
)
rb = (
    drawing.two_terminal(parts["RB"])
    .at(timer.DISCH)
    .toy(timer.THRESH)
    .label_ref(loc="left", offset=8)
    .label_value(loc="left", offset=22)
    .idot()
    .dot()
)
```

The builder is deferred: the element is not placed until a property or materializing method
is accessed. After placement, the cursor advances to the drop endpoint (default `"end"`).

Key builder methods:
- `.at(anchor)` — set the anchor point
- `.to(anchor)`, `.tox(anchor_or_x)`, `.toy(anchor_or_y)` — set the end anchor
- `.right()`, `.left()`, `.up()`, `.down()` — set orientation (optionally with length)
- `.length(value)` — set element length in drawing units
- `.reverse()` — swap start/end pins for presentation
- `.flip()` — flip the symbol across the placement axis
- `.anchor("start"|"end"|"center")`, `.drop("start"|"end")` — control which terminal is
  fixed and which drives the cursor
- `.label_ref(loc=, offset=)`, `.label_value(loc=, offset=)` — emit symbol fields
- `.dot()`, `.idot()` — add junction dots at the end or start terminal
- `.start`, `.end`, `.center` — pin anchors after materialization

Full builder reference: `references/two-terminal-builder.md`.

### Placing Multi-Pin Symbols

Use `drawing.place()` for ICs, connectors, and any component that is not strictly
two-terminal:

```python
timer = (
    drawing.place(parts["U1"])
    .label_ref(loc="top", offset=4)
    .label_value(loc="bottom", offset=14, align="end")
)
```

`drawing.place()` positions the symbol at `drawing.here` by default. Pass `at=` and
`orient=` to override. The returned `PlacedSchematicElement` exposes named pin anchors as
attributes (`timer.VCC`, `timer.DISCH`) and by index (`timer[1]`).

### Wires

After placements, connect them:

```python
# Infer net from two pin anchors on the same logical net
drawing.connect(timer.THRESH, timer.TRIG, shape="-")

# Explicit net, useful for offset and label anchors
drawing.wire(nets["GND"]).at(timing_cap.end).tox(ground.pin).direct()
```

`drawing.connect(start, end)` infers the net when both endpoints are pin anchors on the
same logical net. Pass `net=` when using coordinate or label anchors. `drawing.wire(net)`
returns a `SchematicWireBuilder` starting at the cursor; chain `.from_()` / `.via()` /
`.to()` / `.tox()` / `.toy()` then call `.direct()` or `.orthogonal()`.

For repeated fanout from a connector or IC:

```python
drawing.ortho_lines(
    (
        (nets["GND"], timing_cap.end, ground.pin),
        (nets["GND"], control_cap.end, ground.pin),
    ),
    shape="-|",
    k=-18,
)
```

### Power, Ground, Labels, Ports

All these helpers visualize existing nets; they never create connectivity:

```python
drawing.power_stub("+5V", at=timer.VCC)
drawing.ground_stub("GND", at=timing_cap.end)
drawing.local_label(nets["TIMING"], at=timing_cap.start, side="Left", offset=34, orient="Right")
drawing.signal_tag(nets["RESET"], at=some_pin, side="Right")
drawing.junction(nets["GND"], at=some_point)
drawing.no_connect(placed.pin("NC"), reason="unused by design")
drawing.sheet_port("SWDIO", net=swdio, at=anchor, kind="Input")
drawing.off_page("SWDIO", net=swdio, at=anchor)
```

Pass `net=` whenever the anchor is a coordinate, a label anchor, or an offset anchor that
does not prove the net by itself. Pin anchors can infer the net when the pin is already
connected in the logical circuit.

### Cursor Tools

Use `hold()` to branch from the current cursor without advancing it:

```python
with drawing.hold():
    drawing.move_from(timer.DISCH.left(36).up(22))
    ra = drawing.two_terminal(parts["RA"]).down().label_ref().label_value()
```

Use `push()` / `pop()` for the same save-and-restore behaviour without a context manager.
Use `move(dx=, dy=)` to shift the cursor by a relative offset. Use
`move_from(anchor, dx=, dy=)` to jump to an anchor with an optional offset.

`drawing.stack(count=N, direction="Down", pitch=20)` returns N evenly spaced anchors from
the current cursor position, useful for placing rows of connectors or caps.

Full session reference: `references/drawing-session.md`.

## Fallback: Low-Level Placement API

When the drawing session does not cover a case (for example, arbitrary IC placement with
explicit coordinates, or direct wire list authoring), use the low-level `sch.place()` and
`sch.wire()` API directly on the `Schematic` object:

```python
r_sym = sch.place(r1, at=(40, 20), orient="Right", symbol="resistor")
sch.wire(led_a).from_(r_sym.pin(2)).to(d_sym.pin("A")).orthogonal()
```

This API is documented in `references/low-level-api.md` and in `docs/python-api.md`
section "Schematic Placement".

## Layout for Reading

Group components by function before detailed routing: power, IC core, connectors, timing,
protection, indicators. Prefer functional signal flow direction over raw package pin order.
Keep a consistent direction for power rails (up) and ground returns (down). Route wires
orthogonally; reserve shaped routes (`shape="-|"`, `shape="|-"`, etc.) for fanout.

Use named regions on the sheet to document functional boundaries:

```python
sheet.region("timing", x=100, y=40, w=80, h=60, title="Timing Network")
```

## Visual Quality Rubric

A clean model test is necessary but not sufficient. Render the sheet and **view the SVG as an image** (see "Viewing Rendered Output" in `../shared-volt-architecture.md` for how to view or rasterize it), then verify:

1. **Tag and port restraint** — power/ground stubs and off-page tags are quiet annotations,
   not the dominant visual element. Avoid excessive tagging when a short wire communicates
   the same information more clearly.
2. **Consistent spacing** — element-to-element gaps follow the chosen `unit`. Connector
   rows are evenly pitched.
3. **Aligned labels** — reference and value labels sit on the same side of each element
   and are visually aligned within a functional group.
4. **Thin wires and small junction dots** — wires do not overwhelm the symbol body; dots
   appear only at real T-junctions.
5. **Whitespace** — no symbol or wire cluster is so dense that a reader cannot trace a net
   in under three seconds.
6. **No collisions or overflow** — labels, wires, and symbols do not overlap; nothing
   extends outside the usable sheet area.
7. **No ambiguous crossings** — wires that cross without a junction dot must visibly pass
   at distinct angles or be re-routed.
8. **Split sheets when needed** — if one page cannot remain readable at the chosen sheet
   size, split the design into multiple named sheets, each focused on a functional block.

Always view the rendered SVG as an image — not just the JSON or diagnostics. Diagnostics
(`SCHEMATIC_PIN_NET_NOT_VISUALLY_COVERED`, `SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED`) are
necessary checks but will not catch crowded layouts, poorly aligned labels, or crossed
wires — only your eyes on the drawing will.

## Validation Checklist

- `sheet.validate()` — no unexpected diagnostics, especially not
  `SCHEMATIC_PIN_NET_NOT_VISUALLY_COVERED` or `SCHEMATIC_NO_CONNECT_INTENT_NOT_MARKED`.
- Inspect schematic JSON: every `ComponentId`, `PinId`, and `NetId` reference resolves to
  an existing logical entity.
- View the rendered SVG as an image: verify grouping, label alignment, tag restraint,
  spacing, no collisions, title block fit.
- Add `@project.schematic.test` entries for durable contracts such as required component
  placement.

## References

- `references/drawing-session.md` — drawing session toolbox (`two_terminal`, `place`,
  `connect`, `wire`, `power_stub`, `ground_stub`, `local_label`, `signal_tag`, `junction`,
  `hold`, `push`/`pop`, `move`, `move_from`, `stack`, `frame`, `node`, `ortho_lines`).
- `references/two-terminal-builder.md` — `SchematicTwoTerminalElement` full chain.
- `references/low-level-api.md` — fallback `sch.place()`/`sch.wire()` and
  `design.schematic()` argument table.
- `references/walkthrough-555-schematic.md` — annotated read of
  `examples/timer_555_led_blinker/schematic.py`.
- `docs/python-api.md` section "Schematic Placement".
- `docs/design/schemdraw-style-schematic-authoring.html`.
- `examples/timer_555_led_blinker/schematic.py`.
- `examples/schematic_sugar/timer_555_led_blinker.py`.
- `examples/schematic_sugar/compact_led.py`.
