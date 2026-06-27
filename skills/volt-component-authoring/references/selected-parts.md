# Selected Parts Cookbook

Source: `examples/timer_555_led_blinker/components.py`

---

## `Component.select_part()` signature

Defined in `python/volt/logical.py` (line 534):

```python
component.select_part(
    *,
    manufacturer: str,
    part_number: str,
    package: str,
    footprint: FootprintInput,           # Footprint object or (library, name) tuple
    pin_pads: dict[int | str, str],      # logical pin → physical pad label
    properties: dict | None = None,
    voltage_rating: float | None = None,
    power_rating: float | None = None,
    model_3d: PartModel3D | None = None,
    approved_alternate_mpns: Iterable[str] = (),
    selection_override: bool = False,
) -> Component
```

Returns `self` for method chaining. All keyword args are required except the optional ones shown.

---

## Pattern 1: Passive (pin-number keys)

Use integer pin numbers as `pin_pads` keys when the logical component uses numbered pins (resistors, capacitors, generic passives). Key type is `int`; value is the pad label string.

Quoted verbatim from `examples/timer_555_led_blinker/components.py`:

```python
parts["J1"].select_part(
    manufacturer="JST",
    part_number="S2B-PH-SM4-TB(LF)(SN)",
    package="JST-PH-SMD-1x02",
    footprint=FOOTPRINTS["jst_ph_smd_1x02"],
    pin_pads={1: "1", 2: "2"},
)
```

For the three resistors (in a bulk loop):

```python
for component, part_number, power_rating in (
    (parts["RA"],   "RMCF0805FT100K", 0.125),
    (parts["RB"],   "RMCF0805FT47K0", 0.125),
    (parts["RLED"], "RMCF0805FT1K00", 0.125),
):
    component.select_part(
        manufacturer="Stackpole",
        part_number=part_number,
        package="0805",
        footprint=FOOTPRINTS["resistor_0805"],
        pin_pads={1: "1", 2: "2"},
        power_rating=power_rating,
    )
```

---

## Pattern 2: IC (pin-name keys)

Use string pin names as `pin_pads` keys when the logical component uses named pins defined via `PinSpec`. The names must match the `PinSpec` `name` values exactly.

Quoted verbatim from `examples/timer_555_led_blinker/components.py`:

```python
timer.select_part(
    manufacturer="Texas Instruments",
    part_number="NE555DR",
    package="SOIC-8",
    footprint=FOOTPRINTS["timer_soic_8"],
    pin_pads={
        "GND":    "1",
        "TRIG":   "2",
        "OUT":    "3",
        "RESET":  "4",
        "CTRL":   "5",
        "THRESH": "6",
        "DISCH":  "7",
        "VCC":    "8",
    },
    voltage_rating=16.0,
)
```

---

## Pattern 3: Capacitors with voltage_rating

`voltage_rating` lowers into a typed selected-part electrical attribute. Quoted verbatim:

```python
for component, part_number, voltage_rating in (
    (parts["CT"],    "CL21B105KBFNNNE", 50.0),
    (parts["CCTRL"], "CL21B103KBANNNC", 50.0),
    (parts["CDEC"],  "CL21B104KBCNNNC", 50.0),
):
    component.select_part(
        manufacturer="Samsung Electro-Mechanics",
        part_number=part_number,
        package="0805",
        footprint=FOOTPRINTS["capacitor_0805"],
        pin_pads={1: "1", 2: "2"},
        voltage_rating=voltage_rating,
    )
```

---

## Pattern 4: LED — pin-name keys with polarity direction

The LED logical component uses named pins `"K"` (cathode) and `"A"` (anode). The physical pad labels on the footprint are `"1"` (K) and `"2"` (A). Note the mapping direction: it is `pin_name → pad_label`.

Quoted verbatim from `examples/timer_555_led_blinker/components.py`:

```python
parts["DLED"].select_part(
    manufacturer="Lite-On",
    part_number="LTST-C171KRKT",
    package="0805",
    footprint=FOOTPRINTS["led_0805"],
    pin_pads={"K": "1", "A": "2"},
)
```

The `{"K":"1","A":"2"}` mapping maps the cathode logical pin to physical pad 1 and the anode logical pin to physical pad 2. This is specific to this LED datasheet's pad numbering.

---

## Pattern 5: Bulk loop

When multiple components share a manufacturer/package but differ by part number or rating, loop over a list of tuples:

```python
for component, part_number, power_rating in (
    (parts["RA"],   "RMCF0805FT100K", 0.125),
    (parts["RB"],   "RMCF0805FT47K0", 0.125),
    (parts["RLED"], "RMCF0805FT1K00", 0.125),
):
    component.select_part(
        manufacturer="Stackpole",
        part_number=part_number,
        package="0805",
        footprint=FOOTPRINTS["resistor_0805"],
        pin_pads={1: "1", 2: "2"},
        power_rating=power_rating,
    )
```

---

## Ratings

| Argument | Dimension | Typical use |
|---|---|---|
| `voltage_rating` | voltage (V) | ICs, capacitors, MOSFETs |
| `power_rating` | power (W) | Resistors |

Both lower into typed selected-part electrical attributes in logical JSON.

---

## Kernel validation rules

The kernel rejects `select_part()` calls with:

- A `pin_pads` key that is not a known logical pin name/number.
- Duplicate physical pad labels within one component's mapping.
- Missing logical pins. Every logical pin of the component must map to at least one pad; omitting one is rejected (see `python/tests/test_component_library.py` — `pin_pads={1: "1"}` on a two-pin part raises "missing pin mapping should be rejected").

A logical pin may appear as a key mapping to the same or different pad labels on multiple pads — this is the tied-lands case (e.g. thermally-exposed tab packages where one ground pin fans out to multiple pads).

---

## Downstream identity check

After calling `select_part()`, the logical JSON `selected_physical_part` field for that component should contain: `manufacturer`, `part_number`, `package`, `footprint` (library + name), `pin_pads`, and any selected-part `ratings`. Verify with:

```bash
python -m volt validate examples/timer_555_led_blinker
```
