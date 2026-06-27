# Hello World: Minimal LED Board

A minimal end-to-end Volt project — one LED with a current-limiting resistor and a 2-pin connector — covering all three stages and flat artifact output. This example intentionally stops short of calling `select_part()` on the placed components, so it reports board-readiness diagnostics and `result.ok` is `False` on the first run; full physical-part selection is covered in `volt-component-authoring`.

Adapted from two real source examples:
- `examples/schematic_sugar/compact_led.py`
- `examples/pcb_led_board/main.py`

```python
import volt

# ---------------------------------------------------------------------------
# Project definition
# ---------------------------------------------------------------------------

project = volt.Project("hello-led", version="0.1.0")


# ---------------------------------------------------------------------------
# Stage 1: Logical design
#
# Create nets, instantiate built-in components, and connect pins.
# Return the design plus two ProjectResource values so later stages
# can retrieve the pre-built net and part dicts by name.
# ---------------------------------------------------------------------------

@project.design
def design():
    d = volt.Design("hello-led")

    # Named nets with kind and voltage intent
    vcc = d.net("+3V3", kind="power", voltage=3.3)
    led_a = d.net("LED_A")
    gnd = d.net("GND", kind="ground")

    # Built-in catalog components
    j1 = d.connector_1x02(ref="J1")
    r1 = d.R("330 ohm", ref="R1")
    d1 = d.LED(ref="D1")

    # Connect pins to nets with += (lowers into kernel connectivity)
    vcc += j1[1], r1[1]
    led_a += r1[2], d1["A"]
    gnd += d1["K"], j1[2]

    nets = {"+3V3": vcc, "LED_A": led_a, "GND": gnd}
    parts = {"J1": j1, "R1": r1, "D1": d1}

    # Pass nets and parts forward so schematic/board stages don't re-build them
    return d, volt.ProjectResource("nets", nets), volt.ProjectResource("parts", parts)


# ---------------------------------------------------------------------------
# Stage 2: Schematic
#
# Use the sheet.drawing() sugar API (SchemDraw style) to place components
# and connect them with fluent two_terminal builders and drawing.connect().
# ---------------------------------------------------------------------------

@project.schematic
def schematic(context: volt.BuildContext) -> volt.Schematic:
    d = context.design()
    nets = context.resource("nets", dict)
    parts = context.resource("parts", dict)

    sheet = d.schematic("Main")

    with sheet.drawing(at=(20, 20), unit=20) as drawing:
        # Place the two-terminal resistor flowing right; add ref and value labels
        resistor = (
            drawing.two_terminal(parts["R1"])
            .right()
            .label_ref()
            .label_value()
        )
        # Place the LED flowing right, reversed so anode faces the resistor
        led = (
            drawing.two_terminal(parts["D1"])
            .right()
            .reverse()
            .label_ref()
        )

        # Wire resistor end to LED start (inferred net = LED_A)
        drawing.connect(resistor.end, led.start)

        # Add power and ground symbols at the open ends
        drawing.power("+3V3", at=resistor.start)
        drawing.ground(at=led.end)

        # Net label for readability on the intermediate node
        drawing.net_label(nets["LED_A"], at=resistor.end.up(10))

    return sheet


# ---------------------------------------------------------------------------
# Stage 3: Board
#
# Set the physical outline and layer stack, then use board.layout() sugar
# to place footprints and route copper connections.
# ---------------------------------------------------------------------------

@project.board
def board(context: volt.BuildContext) -> volt.Board:
    d = context.design()
    parts = context.resource("parts", dict)

    pcb = d.board("Main")

    # Layer stack
    front = pcb.add_layer("F.Cu", role="copper", side="top")
    back = pcb.add_layer("B.Cu", role="copper", side="bottom")
    pcb.set_layer_stack((front, back), thickness=1.6)

    # Rectangular board outline (mm)
    pcb.set_rectangular_outline(origin=(0.0, 0.0), size=(32.0, 18.0))

    with pcb.layout(unit=1.0) as layout:
        # Place the connector locked at the left edge
        header = layout.place(
            parts["J1"],
            at=pcb.edge("left").center().right(5.0),
            orient="right",
            locked=True,
        )
        # two_pad() for passive 0603-style components
        resistor = layout.two_pad(parts["R1"]).at((15.0, 7.0)).anchor("center").right()
        led = layout.two_pad(parts["D1"]).at(resistor.center.right(9.0)).anchor("center").left()

        # Route copper on the front layer
        layout.connect(header[1], resistor[1], layer=front, width=0.25, mode="direct")
        layout.connect(resistor[2], led.A, layer=front, width=0.25, mode="direct")
        layout.connect(header[2], led.K, layer=front, width=0.25, mode="direct")

    return pcb


# ---------------------------------------------------------------------------
# Run and output
# ---------------------------------------------------------------------------

result = project.run()

# This minimal example places footprints but does not select physical parts,
# so Volt reports PCB board-readiness diagnostics and result.ok is False until
# you add select_part(...) — see volt-component-authoring and volt-pcb-authoring.
# write_artifacts still renders the schematic and PCB SVGs so you can view the result.
for diagnostic in result.diagnostics:
    print(diagnostic.severity, diagnostic.code, diagnostic.message)

artifacts = result.write_artifacts("dist/hello-led", slug="hello-led")
# artifacts.logical_json       → dist/hello-led/hello-led.volt.json
# artifacts.schematic_svg      → dist/hello-led/hello-led.svg
# artifacts.pcb_svg            → dist/hello-led/hello-led.pcb.svg
# artifacts.diagnostics_json   → dist/hello-led/hello-led.validation.json
```

## What Each Section Shows

| Block | Volt concept |
|---|---|
| `volt.Project` + decorators | Staged product workflow |
| `d.net(..., kind=, voltage=)` | Named nets with typed electrical intent |
| `d.R(...)`, `d.LED(...)`, `d.connector_1x02(...)` | Built-in catalog helpers |
| `net += pins` | Kernel connectivity mutations |
| `volt.ProjectResource("nets", nets)` | Passing data from design to later stages |
| `context.design()`, `context.resource(...)` | Consuming data from BuildContext |
| `sheet.drawing(...)` + `two_terminal(...)` | SchemDraw-style schematic sugar |
| `drawing.connect(...)`, `drawing.power(...)`, `drawing.ground(...)` | Wire runs and schematic ports |
| `board.layout(...)` + `two_pad(...)` | PCB layout sugar |
| `layout.connect(...)` | Copper routing on named layers |
| `result.write_artifacts(...)` | Flat output for viewers |
