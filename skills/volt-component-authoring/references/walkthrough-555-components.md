# Walkthrough: 555 Blinker components.py

This is a narrated end-to-end read of `examples/timer_555_led_blinker/components.py`. Read the source alongside this walkthrough.

---

## File overview

The file has three logical sections:

1. **Module-level constants** — the footprint helper, the `FOOTPRINTS` dict, and the two `SchematicSymbolSpec` objects.
2. **`build_design()`** — creates the `Design`, defines custom components, creates nets and instances, wires connectivity, selects parts, sets DNP.
3. **Returns** `(design, nets, parts)` — a common Volt pattern for example/test separation.

---

## Section 1: The `_front_smd_pad` helper

```python
def _front_smd_pad(
    label: str,
    *,
    at: tuple[float, float],
    size: tuple[float, float],
    shape: str = "rounded_rectangle",
    mechanical_role: str | None = None,
) -> volt.FootprintPad:
    return volt.FootprintPad.surface_mount(
        label,
        at=at,
        size=size,
        shape=shape,
        mechanical_role=mechanical_role,
    )
```

This is a file-local helper that forwards to the real API `volt.FootprintPad.surface_mount(...)`, fixing `layers="front_smd"` (the default). It exists purely to reduce repetition in the `FOOTPRINTS` dict below.

---

## Section 2: TIMER_SYMBOL

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

Key points:
- The `name` argument `"volt.examples.timer_555_led_blinker:NE555"` is a namespace-qualified symbol ID. It is the same namespace/name used in the `source=` argument of `define_component`.
- `ic_pin` arguments: pin logical name, physical pin number, side, slot (1-based position along that side), optional display label.
- Left/right sides carry signal pins grouped by function. Top/bottom carry power and reset.
- `pin_numbers=True` enables physical pin number display on the rendered symbol.

---

## Section 3: SUPPLY_SYMBOL

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

This uses the lower-level `SchematicSymbolSpec(name, pins, primitives)` constructor. The two pin anchors are at `(0,0)` and `(0,8)`. Two horizontal lines connect pin anchors to the rectangle body at `x=8`. The `"J"` text label sits below the box at `(15,-10)`. Pin names `"1"` and `"2"` must match the `PinSpec` names in `define_component`.

---

## Section 4: FOOTPRINTS dict

Five footprints are defined: a JST PH 2-pin SMD connector (with two mechanical support pads), an 8-pin SOIC for the timer IC, and three 0805-sized footprints for resistors, capacitors, and LED.

See `references/footprints.md` for the full quoted FOOTPRINTS dict and the pad coordinate tables.

Notable: the JST connector adds `"MP1"` and `"MP2"` with `mechanical_role="mechanical_support"`. These pads are not included in `pin_pads` when calling `select_part` because they carry no logical connectivity.

---

## Section 5: build_design() — component definitions

```python
design = volt.Design("timer-555-led-blinker")
supply_definition = design.define_component(
    "ExternalSupply",
    source=("volt.examples.timer_555_led_blinker", "external_supply", "1.0.0"),
    pins=[
        volt.PinSpec("1", 1, role="power_output"),
        volt.PinSpec("2", 2, role="ground"),
    ],
    properties={"category": "validation_source"},
    schematic_symbol=SUPPLY_SYMBOL,
)
timer_definition = design.define_component(
    "NE555",
    source=("volt.examples.timer_555_led_blinker", "ne555", "1.0.0"),
    pins=[
        volt.PinSpec("GND",    1, role="ground"),
        volt.PinSpec("TRIG",   2, role="analog_input"),
        volt.PinSpec("OUT",    3, role="output", signal="digital"),
        volt.PinSpec("RESET",  4, role="input",  signal="digital"),
        volt.PinSpec("CTRL",   5, role="analog_input"),
        volt.PinSpec("THRESH", 6, role="analog_input"),
        volt.PinSpec("DISCH",  7, role="analog_output"),
        volt.PinSpec("VCC",    8, role="power"),
    ],
    schematic_symbol=TIMER_SYMBOL,
)
```

Two custom definitions: a supply connector and a 555 timer IC. Both carry `source=` tuples for stable identity. `role` is authoring shorthand; `signal="digital"` on OUT and RESET is a kernel-owned typed attribute that will appear in logical JSON.

---

## Section 6: Nets

```python
nets = {
    "+5V":   design.net("+5V",  kind="power",  voltage=5.0),
    "GND":   design.net("GND",  kind="ground"),
    "DISCH": design.net("DISCH"),
    "TIMING":design.net("TIMING"),
    "CTRL":  design.net("CTRL"),
    "OUT":   design.net("OUT"),
    "LED_A": design.net("LED_A"),
}
```

`+5V` is a typed power net at 5 V. `GND` is a typed ground net. The remaining nets are signal nets. All nets are created before connectivity is established — this is the standard Volt authoring order.

---

## Section 7: Instances

```python
parts = {
    "J1":   design.instantiate(supply_definition, ref="J1"),
    "U1":   design.instantiate(timer_definition,  ref="U1", properties={"value": "NE555"}),
    "RA":   design.R("100 kOhm", ref="R1"),
    "RB":   design.R("47 kOhm",  ref="R2"),
    "CT":   design.C("1 uF",     ref="C1"),
    "CCTRL":design.C("10 nF",    ref="C2"),
    "CDEC": design.C("100 nF",   ref="C3"),
    "RLED": design.R("1 kOhm",   ref="R3"),
    "DLED": design.LED(ref="D1"),
}
```

Custom definitions are instantiated via `design.instantiate(definition, ref=...)`. Built-in catalog parts (`R`, `C`, `LED`) are instantiated directly and returned as `Component` handles. The dict key is the internal Python label used for wiring; the `ref` is the schematic reference designator.

---

## Section 8: Connectivity

```python
timer = parts["U1"]
nets["+5V"] += (
    parts["J1"][1],
    timer["VCC"],
    timer["RESET"],
    parts["RA"][1],
    parts["CDEC"][1],
)
nets["DISCH"] += timer["DISCH"], parts["RA"][2], parts["RB"][1]
nets["TIMING"] += timer["TRIG"], timer["THRESH"], parts["RB"][2], parts["CT"][1]
nets["CTRL"]   += timer["CTRL"], parts["CCTRL"][1]
nets["OUT"]    += timer["OUT"],  parts["RLED"][1]
nets["LED_A"]  += parts["RLED"][2], parts["DLED"]["A"]
nets["GND"] += (
    parts["J1"][2],
    timer["GND"],
    parts["CT"][2],
    parts["CCTRL"][2],
    parts["CDEC"][2],
    parts["DLED"]["K"],
)
```

`component[pin_name_or_number]` returns a `Pin` handle. `net += pin` (or a tuple of pins) connects pins to a net. Custom components use string pin names (`timer["VCC"]`). Built-in catalog parts use integer pin numbers (`parts["RA"][1]`). The LED uses named pins `"A"` and `"K"`.

---

## Section 9: Selected parts

See `references/selected-parts.md` for the full quoted `select_part` calls and a breakdown of each pattern.

Brief summary of what this example covers:

| Component | Key type | Special args |
|---|---|---|
| J1 (connector) | `{1:"1", 2:"2"}` int keys | — |
| U1 (NE555) | `{"GND":"1", ...}` string keys | `voltage_rating=16.0` |
| RA, RB, RLED (resistors) | `{1:"1", 2:"2"}` int keys | `power_rating=0.125` |
| CT, CCTRL, CDEC (caps) | `{1:"1", 2:"2"}` int keys | `voltage_rating=50.0` |
| DLED (LED) | `{"K":"1","A":"2"}` string keys | — |

---

## Section 10: DNP

```python
for component in parts.values():
    component.dnp(False)
```

Iterates every component and marks it `dnp(False)` — all parts are fitted. In a real design you would only call `dnp(True)` for parts that should be unpopulated. `dnp(False)` is an explicit assertion of fit intent, which makes the BOM/CPL exporters confident rather than assuming a default.

---

## Return value

```python
return design, nets, parts
```

Returning `(design, nets, parts)` lets callers (example runners, tests, schematic/PCB authoring code) unpack the completed design object and named handles without rerunning definitions. This separation pattern is recommended for any multi-file Volt project.
