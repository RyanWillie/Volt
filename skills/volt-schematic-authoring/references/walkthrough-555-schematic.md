# Walkthrough: 555 LED Blinker Schematic

**Primary source:** `examples/timer_555_led_blinker/schematic.py`.
**Second example:** `examples/schematic_sugar/timer_555_led_blinker.py`.
**Third example:** `examples/schematic_sugar/compact_led.py`.

This walkthrough narrates how the canonical 555 LED blinker schematic is authored step by
step, explaining why each choice produces a readable result.

---

## Step 1: Sheet Setup

`schematic.py` opens by calling `design.schematic()` with a full title block:

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

A 340 × 240 mm landscape sheet is generous for eight components. The 5 mm grid provides
snap discipline without being visible in the output. Coordinate zones give the title block
its A1–D4 zone labels.

---

## Step 2: Opening the Drawing Session

```python
with sheet.drawing(at=(140, 80), unit=20) as drawing:
```

`at=(140, 80)` places the cursor near the centre-left of the sheet, where the 555 IC will
land. `unit=20` means each `.right()`, `.down()`, etc. call moves 20 mm by default, which
matches the pin pitch of the IC symbol.

---

## Step 3: Place the IC and Connector

```python
header = drawing.place(parts["J1"], at=(72, 84), orient="Right").label_ref(loc="left")
timer = (
    drawing.place(parts["U1"])
    .label_ref(loc="top", offset=4)
    .label_value(loc="bottom", offset=14, align="end")
)
```

The connector `J1` is placed with an explicit `at=` because it does not follow from the
cursor (it is off to the left of the IC). `U1` uses the cursor as established by `at=` on
the session. Both return `PlacedSchematicElement` handles whose named pin attributes
(`timer.DISCH`, `timer.VCC`, `timer.THRESH`, `timer.TRIG`, `timer.CTRL`, `timer.RESET`,
`timer.OUT`, `timer.GND`) drive every subsequent placement anchor.

---

## Step 4: Power and Ground Stubs on the IC

```python
ground = drawing.ground("GND", at=timer.GND, orient="Down")
drawing.power_stub("+5V", at=header[1], net=nets["+5V"], side="Up", length=20)
drawing.ground_stub("GND", at=header[2], net=nets["GND"], side="Down", length=20)
```

`drawing.ground("GND", at=timer.GND, orient="Down")` places the ground symbol directly at
the GND pin anchor. The returned `SchematicPort` handle (`ground`) has a `.pin` property
used later as the ground reference point for all downward ground stubs.

The connector pins need explicit `net=` because `header[1]` is a pin anchor but the wire
target is offset (`side="Up", length=20`).

---

## Step 5: Timing Network (Two-Terminal Builders)

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

- `ra` is placed vertically above DISCH, from DISCH+unit to DISCH. `.at()` and `.to()` set
  both terminals; orientation is inferred from the direction between them.
- `rb` runs from DISCH down to the y-coordinate of THRESH using `.toy()`. Junction dots
  `.idot()` and `.dot()` mark the T-junctions at both ends.
- `timing_cap` runs from TRIG down to the y-coordinate of `ground.pin`, again with
  T-junctions at both ends (because THRESH is also connected to the same node).

The `loc="left"` label placement keeps reference and value labels on the left side of this
vertical stack, out of the IC body.

---

## Step 6: Control and Decoupling Caps

```python
control_cap = (
    drawing.two_terminal(parts["CCTRL"])
    .at(timer.CTRL)
    .toy(ground.pin)
    .label_ref(loc="right", offset=8)
    .label_value(loc="bottom", offset=8)
    .dot()
)
decoupling_cap = (
    drawing.two_terminal(parts["CDEC"])
    .at(timer.VCC.right(80))
    .down()
    .label_ref(loc="right", offset=8)
    .label_value(loc="right", offset=24)
)
```

`control_cap` uses `loc="right"` because CTRL is on the right side of the IC. The
`decoupling_cap` is offset 80 mm to the right of VCC and placed downward with `.down()`.

---

## Step 7: Output Path (LED and Resistor)

```python
led_resistor = (
    drawing.two_terminal(parts["RLED"])
    .at(timer.OUT)
    .right()
    .label_value(loc="top", offset=8)
)
led = (
    drawing.two_terminal(parts["DLED"])
    .reverse()
    .toy(ground.pin)
    .label_ref(loc="right", offset=14)
)
```

`led_resistor` starts at the OUT pin and runs right using `.right()` (one unit). The
cursor then sits at `led_resistor.end`. `led` uses `.reverse()` so the cathode (K) is the
drop terminal pointing downward, and `.toy(ground.pin)` extends it to the ground rail.
`.reverse()` changes only presentation; the logical net membership is unchanged.

---

## Step 8: Power Stubs and Cross-Connects

```python
drawing.power_stub("+5V", at=timer.VCC)
drawing.connect(timer.RESET, timer.VCC, shape="-").dot()
drawing.power_stub("+5V", at=ra.start)
drawing.power_stub("+5V", at=decoupling_cap.start)
drawing.ground_stub("GND", at=decoupling_cap.end)
```

A plain `drawing.connect(timer.RESET, timer.VCC, shape="-")` with `shape="-"` draws a
direct horizontal wire tying RESET to VCC. The `.dot()` call on the returned wire adds the
junction where it meets the VCC power stub above. This is a schematic presentation choice;
the logical net already connects RESET and VCC.

---

## Step 9: Shared Ground Wires

```python
drawing.connect(timer.THRESH, timer.TRIG, shape="-")
drawing.wire(nets["GND"]).at(timing_cap.end).tox(ground.pin).direct()
drawing.wire(nets["GND"]).at(control_cap.end).tox(ground.pin).direct()
drawing.wire(nets["GND"]).at(led.end).tox(ground.pin).direct()
```

`drawing.wire(net).at(anchor).tox(target).direct()` routes a horizontal wire from the
given anchor to the x-coordinate of `ground.pin`. This is cleaner than `connect()` here
because the wire is a pure horizontal segment from an offset anchor.

`drawing.connect(timer.THRESH, timer.TRIG, shape="-")` uses inferred net (both pins are
on `nets["TIMING"]`).

---

## Step 10: Local Label

```python
drawing.local_label(
    nets["TIMING"],
    at=timing_cap.start,
    side="Left",
    offset=34,
    orient="Right",
)
```

A `local_label` places a readable net name annotation near the TIMING node without drawing
an explicit bus line. `offset=34` pushes it clear of the `rb` and `timing_cap` labels on
the same side. This is a readability annotation only; it does not change net membership.

---

## Why the Result Reads Cleanly

- **IC at the centre.** All passives are anchored to IC pin positions, so routing is short
  and direct.
- **Consistent vertical direction for passives.** RA, RB, CT, CCTRL all run vertically on
  the left; RLED and DLED run rightward from the OUT pin. The reader's eye follows the
  signal path left to right.
- **Power rails at top, ground at bottom.** `power_stub("+5V")` points up; `ground_stub()`
  points down. No net crosses its own rail.
- **T-junctions marked.** `.idot()` and `.dot()` on RB and CT make shared nodes
  unambiguous. The `drawing.connect().dot()` on the RESET/VCC wire marks that junction
  too.
- **Labels on one side per group.** Left-side labels for the vertical timing stack;
  right-side labels for the right-side cap and LED path.

---

## Second Worked Example: `examples/schematic_sugar/timer_555_led_blinker.py`

The sugar example uses `ortho_lines()` for the fanout instead of individual `wire()` calls:

```python
drawing.ortho_lines(
    (
        (nets["+5V"], source[1], vcc),
        (nets["GND"], source[2], ground),
        (nets["+5V"], timer.VCC, vcc),
        (nets["+5V"], timer.RESET, vcc),
        (nets["+5V"], ra.start, vcc),
        (nets["DISCH"], timer.DISCH, rb.start),
        (nets["DISCH"], ra.end, rb.start),
        (nets["TIMING"], timer.THRESH, timing_cap.start),
        (nets["TIMING"], timer.TRIG, timing_cap.start),
        (nets["CTRL"], timer.CTRL, control_cap.start),
        (nets["OUT"], timer.OUT, led_resistor.start),
        (nets["LED_A"], led_resistor.end, led.start),
        (nets["GND"], timer.GND, ground),
        (nets["GND"], timing_cap.end, ground),
        (nets["GND"], control_cap.end, ground),
        (nets["GND"], led.end, ground),
    ),
    shape="-|",
    k=-18,
)
```

And uses `drawing.hold()` to branch placements without advancing the main cursor:

```python
with drawing.hold():
    drawing.move_from(timer.DISCH.left(36).up(22))
    ra = drawing.two_terminal(parts["RA"]).down().label_ref(loc="left").label_value(loc="right")
    rb = drawing.two_terminal(parts["RB"]).at(timer.DISCH.left(36)).down().label_value(loc="left")
    timing_cap = (
        drawing.two_terminal(parts["CT"])
        .at(rb.end)
        .down()
        .label_ref(loc="left")
        .label_value(loc="right")
    )
```

`drawing.hold()` is ideal when a group of placements should not disturb the main drawing
cursor. After the `with` block exits, the cursor is exactly where it was before.

---

## Third Example: `examples/schematic_sugar/compact_led.py`

The compact LED example shows the minimal sugar pattern — two components, one power, one
ground, and a net label:

```python
with sheet.drawing(at=(20, 20), unit=20) as drawing:
    resistor = drawing.two_terminal(parts["R1"]).right().label_ref().label_value()
    led = drawing.two_terminal(parts["D1"]).right().reverse().label_ref()

    drawing.connect(resistor.end, led.start)
    drawing.power("+3V3", at=resistor.start)
    drawing.ground(at=led.end)
    drawing.net_label(nets["LED_A"], at=resistor.end.up(10))
```

`drawing.connect(resistor.end, led.start)` infers `nets["LED_A"]` because both endpoints
are pin anchors on the same logical net. `drawing.power("+3V3", at=resistor.start)` infers
the net from the pin anchor. `drawing.ground(at=led.end)` likewise. The `net_label` places
a readable annotation at an offset anchor, so `net=` is not required (the anchor is still a
pin anchor).
