# Walkthrough: 555 LED Blinker Routing

Narrated read of the routing section of `examples/timer_555_led_blinker/board.py`.
The full file is the canonical reference; this walkthrough focuses on the routing block
(approximately lines 93–154) and shows how the relative-first principle and `snap` work
in a real board.

**Source:** `examples/timer_555_led_blinker/board.py` — all code blocks below are verbatim
quotes from that file.

---

## Board context

The 555 LED blinker is a 48 × 32 mm two-layer board. The placement block (handled before
routing) establishes:

- `header` — the 2-pin power header, locked to the left edge.
- `timer` — the NE555 IC (U1), locked at (23, 15) as the central anchor.
- `ra`, `rb` — timing resistors, placed in a `hold()` block relative to each other.
- `timing_junction` — a snapped intermediate node between `rb` and `ct`.
- `ct` — timing capacitor, dropped below `timing_junction`.
- `cctrl`, `cdec` — control and decoupling caps.
- `rled`, `dled` — LED series resistor and LED.

None of these coordinates were invented; they were derived from `board.edge("left")`,
absolute anchor coordinates for the IC, and relative offsets from those anchors.

---

## 1. The `horizontal_drop` helper

```python
def horizontal_drop(pad, dx):
    return pad.tox(layout.snap_x(pad.x + dx))
```

This is a local function defined inside the layout block. It takes a pad anchor and a
horizontal offset, and returns a new anchor at the same y as the pad but at an x
coordinate snapped to the grid. It uses `layout.snap_x()` — not a raw arithmetic
expression — so the result is always on the 0.5 mm grid.

`pad.tox(target_x)` is `BoardAnchor.tox`: "same y, move to x".

---

## 2. GND backbone: drops + vias + back-layer stitch

```python
gnd_drops = (
    (header[2], horizontal_drop(header[2], 2.0)),
    (timer.GND, horizontal_drop(timer.GND, -2.0)),
    (cdec.end, horizontal_drop(cdec.end, 1.55)),
    (cctrl.end, horizontal_drop(cctrl.end, 1.6)),
    (ct.end, horizontal_drop(ct.end, 1.65)),
    (dled.K, horizontal_drop(dled.K, -1.6)),
)
for pad, drop in gnd_drops:
    layout.route(nets["GND"], layer=front, width=0.30).at(pad).to(drop)
    layout.via(
        nets["GND"],
        at=drop,
        start_layer=front,
        end_layer=back,
    )
gnd_backbone = tuple(drop for _pad, drop in gnd_drops)
for start, end in zip(gnd_backbone, gnd_backbone[1:]):
    layout.route(nets["GND"], layer=back, width=0.30).at(start).to(end)
```

**What's happening:**

1. `gnd_drops` is a tuple of `(pad_anchor, drop_anchor)` pairs. Every GND pad gets a
   short horizontal drop to a nearby via site. The drop anchor is derived entirely from
   the pad anchor via `horizontal_drop` — no bare coordinates.

2. The `for pad, drop in gnd_drops:` loop:
   - Routes a 0.30 mm GND segment on the front layer from each pad to its drop point.
   - Drops a via at the drop point to switch to the back layer.
   `layout.via(net, at=drop, start_layer=front, end_layer=back)` is the exact call —
   note that `at=` is keyword-only and `start_layer` / `end_layer` are the layer indices
   returned by `board.add_layer(...)`.

3. `gnd_backbone` collects the drop points. Then `zip(gnd_backbone, gnd_backbone[1:])`
   iterates consecutive pairs and routes them together on the back layer, forming a
   continuous spine. This is the "GND backbone" pattern: short per-pad escapes on front,
   one continuous backbone on back.

**Relative anchor observation:** Every drop anchor was derived from the pad anchor using
`horizontal_drop`, which itself calls `pad.tox(layout.snap_x(...))`. No coordinate was
hand-measured. If a part moves during placement revision, the drops and backbone update
automatically.

---

## 3. +5V power rail

```python
power_rail = layout.snap(header[1].up(5.15))
layout.route(nets["+5V"], layer=front, width=0.30).at(header[1]).toy(
    power_rail
).tox(cdec.start).to(cdec.start)
layout.route(nets["+5V"], layer=front, width=0.30).at(cdec.start).to(timer.VCC)
layout.route(nets["+5V"], layer=front, width=0.30).at(cdec.start).toy(
    power_rail
).tox(ra.start).to(ra.start)
layout.route(nets["+5V"], layer=front, width=0.25).at(timer.RESET).tox(
    timer.RESET.left(3.025)
).toy(timer.VCC.up(2.095)).tox(timer.VCC).to(timer.VCC)
```

**What's happening:**

`power_rail` is a snapped anchor: `header[1].up(5.15)` then snapped to the 0.5 mm grid.
This becomes the shared y-coordinate for the horizontal +5V trunk — a "bus" at a fixed
elevation.

The first route leaves `header[1]`, goes vertically to `power_rail` via `.toy(power_rail)`,
then runs horizontally to `cdec.start` via `.tox(cdec.start)`, then terminates at
`cdec.start` via `.to(cdec.start)`. The route has two bends: one vertical, one horizontal.
In octilinear mode, each `.tox()` / `.toy()` step is a single horizontal or vertical
segment, and `.to(pad)` auto-inserts the final clean corner if needed.

The second route continues the +5V tree from `cdec.start` to `timer.VCC`.

The third route branches from `cdec.start` back up to the power rail and then runs to
`ra.start` — the same trunk elevation is reused.

The fourth route handles RESET→VCC with a multi-bend path using three axis projections
chained together. `timer.RESET.left(3.025)` is a relative anchor offset; `.tox(...)` runs
horizontally to reach it; `.toy(timer.VCC.up(2.095))` drops to just above VCC; `.tox(timer.VCC)`
crosses to VCC's x coordinate; `.to(timer.VCC)` terminates.

**Snap in action:** `power_rail = layout.snap(header[1].up(5.15))` is the canonical snap
pattern — compute a derived anchor, snap it to the grid, store it in a variable, reference
it in multiple routes. Without the snap, floating-point arithmetic could push the trunk off
the 0.5 mm grid.

---

## 4. Per-net escapes

```python
disch_escape = layout.snap(timer.DISCH.right(2.5))
disch_bus = layout.snap(ra.end.right(1.6).down(2.0))
layout.route(nets["DISCH"], layer=front).at(timer.DISCH).tox(disch_escape).to(
    rb.start
)
layout.route(nets["DISCH"], layer=front).at(timer.DISCH).tox(disch_escape).toy(
    disch_bus
).tox(disch_bus).toy(ra.end).to(ra.end)

timing_escape = layout.snap(timer.TRIG.right(3.0))
layout.route(nets["TIMING"], layer=front).at(timer.TRIG).tox(timing_escape).toy(
    timer.THRESH
).to(timer.THRESH)
layout.route(nets["TIMING"], layer=front).at(timer.THRESH).tox(
    timing_junction
).toy(timing_junction).to(rb.end)
layout.route(nets["TIMING"], layer=front).at(timing_junction).to(ct.start)

ctrl_escape = layout.snap(timer.CTRL.right(4.5))
layout.route(nets["CTRL"], layer=front).at(timer.CTRL).tox(ctrl_escape).toy(
    cctrl.start
).to(cctrl.start)
out_escape = layout.snap(timer.OUT.right(1.5))
layout.route(nets["OUT"], layer=front).at(timer.OUT).tox(out_escape).toy(
    rled.start
).to(rled.start)
layout.route(nets["LED_A"], layer=front).at(rled.end).toy(dled.A).to(dled.A)
```

**What's happening:**

Each net begins with a short horizontal "escape" from the IC pin. The escape is a snapped
anchor stored in a named variable (`disch_escape`, `timing_escape`, `ctrl_escape`,
`out_escape`). Snapping is critical: the escape is used as a `.tox()` target from the IC
pin, and also as the start of subsequent route segments.

The DISCH net fans out in two directions from the same escape point:
- One branch goes to `rb.start` (the nearby timing resistor).
- The other descends to `disch_bus` (another snapped anchor derived from `ra.end`) and
  connects to `ra.end`.

The TIMING net demonstrates a junction reuse pattern: `timing_junction` (snapped in the
placement block as `layout.snap(rb.end.right(2.0))`) appears as the target of both the
THRESH→junction segment and the junction→`ct.start` segment. The same snapped point is
the anchor for three different route terminals.

**Default width:** Routes that omit `width=` use the grammar default of 0.20 mm. Only
GND and +5V explicitly pass `width=0.30`; RESET passes `width=0.25`. This mirrors rubric 5:
power/ground wider than signal.

---

## Key takeaways

| Pattern | Where it appears |
|---|---|
| Snap before reuse | `power_rail`, `timing_junction`, `disch_escape`, `disch_bus`, all escape anchors |
| `horizontal_drop` via `.tox(layout.snap_x(...))` | GND backbone construction |
| `layout.via(net, at=, start_layer=, end_layer=)` | Every GND via in the backbone loop |
| `.tox()/.toy()` shaping | Every multi-bend route |
| `layout.route(...).at(pad).toy(...).tox(...).to(pad)` | +5V rail, CTRL, OUT, TIMING |
| Width by net class | 0.30 for GND/+5V, 0.25 for RESET, 0.20 (default) for signals |
| `layout.hold()` | Placement blocks; no hold in routing (cursor advances freely) |
