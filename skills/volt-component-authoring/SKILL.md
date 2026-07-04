---
name: volt-component-authoring
description: Author or review Volt component definitions and selected physical parts. Use when defining full Volt components, custom PinSpec data, electrical semantics, selected part metadata, footprints, 3D models, package geometry, BOM/CPL identity, assembly data, or manufacturing identity.
---

# Volt Component Authoring

Use this skill to create or review a complete Volt component record — logical pins through physical manufacturing identity. First read `../shared-volt-architecture.md`.

## Workflow Phases

### Phase 1: Choose built-in catalog or define custom

Check the Design catalog helpers before defining anything:

| Helper | Ref prefix | Notable args |
|---|---|---|
| `design.R(value, *, resistance, tolerance, ref)` | R | value string e.g. `"100 kOhm"` |
| `design.C(value, *, capacitance, voltage_rating, ref)` | C | |
| `design.CP(value, *, capacitance, voltage_rating, ref)` | C | polarized |
| `design.L(value, *, inductance, ref)` | L | |
| `design.LED(*, ref)` | D | |
| `design.diode(*, ref)` | D | |
| `design.connector_1x02(*, ref)` | J | |
| `design.regulator(*, ref)` | U | |
| `design.op_amp(*, ref)` | U | |

These return a `Component` that can receive `select_part()` directly.

### Phase 2: Define a custom component

Use `Design.define_component()` when no built-in helper fits. It returns a `ComponentDefinition` handle. The kernel owns the component model; Python holds only the handle.

```python
timer_definition = design.define_component(
    "NE555",
    source=("volt.examples.timer_555_led_blinker", "ne555", "1.0.0"),
    pins=[
        volt.PinSpec("GND",   1, role="ground"),
        volt.PinSpec("TRIG",  2, role="analog_input"),
        volt.PinSpec("OUT",   3, role="output", signal="digital"),
        volt.PinSpec("RESET", 4, role="input",  signal="digital"),
        volt.PinSpec("CTRL",  5, role="analog_input"),
        volt.PinSpec("THRESH",6, role="analog_input"),
        volt.PinSpec("DISCH", 7, role="analog_output"),
        volt.PinSpec("VCC",   8, role="power"),
    ],
    schematic_symbol=TIMER_SYMBOL,
)
```

See `references/walkthrough-555-components.md` for the full definition block.

**`volt.PinSpec(name, number, ...)` arguments:**

| Argument | Type | Meaning |
|---|---|---|
| `name` | `str` | Stable logical pin name (used as key in `pin_pads`) |
| `number` | `int \| str` | Physical pin number |
| `role` | `str \| None` | Authoring shorthand: `passive`, `input`, `output`, `analog_input`, `analog_output`, `bidirectional`, `power`, `power_input`, `power_output`, `ground`, `no_connect`. Lowers into generic fields; not persisted to logical JSON. |
| `requirement` | `str` | Connection requirement: `required` (default), `optional`, `must_not_connect` |
| `signal` | `str` | Kernel-owned electrical attribute: `digital`, `analog`, or `unspecified` |
| `terminal` | `str` | Terminal style attribute |
| `direction` | `str` | Direction attribute |
| `drive` | `str` | Drive capability attribute |
| `polarity` | `str` | Polarity attribute |
| `voltage_range` | `tuple[float\|None, float\|None]` | `(min_V, max_V)` — kernel-owned, appears in logical JSON |

`signal`, `terminal`, `direction`, `drive`, `polarity`, and `voltage_range` are not Python-only metadata; they lower into kernel-owned pin definitions and logical JSON. ERC consumes them.

**`source` tuple:** `(namespace, name, version)` — all three strings required if provided. Omit for anonymous/inline definitions.

### Phase 3: Instantiate

```python
timer = design.instantiate(timer_definition, ref="U1", properties={"value": "NE555"})
```

For catalog parts (`R`, `C`, `LED`, …) instantiation is implicit in the helper call.

### Phase 4: Select the physical part

Call `component.select_part(...)` on each instance. See `references/selected-parts.md` for all patterns including passive pin-number keys, IC pin-name keys, tied lands, and bulk loops.

```python
timer.select_part(
    manufacturer="Texas Instruments",
    part_number="NE555DR",
    package="SOIC-8",
    footprint=FOOTPRINTS["timer_soic_8"],   # FootprintDefinition object
    pin_pads={"GND":"1","TRIG":"2","OUT":"3","RESET":"4",
              "CTRL":"5","THRESH":"6","DISCH":"7","VCC":"8"},
    voltage_rating=16.0,
)
```

The kernel rejects: missing logical pins, unknown pin names, and duplicate pad labels. A logical pin may map to more than one physical pad for tied-land packages.

### Phase 5: Build footprint geometry

`FootprintDefinition` (alias of `Footprint`) holds the pad list and a library-qualified reference. It can also carry non-pad geometry — `courtyard`, `body`, `fabrication_outline`, `assembly_outline`, and semantic `markings` (`volt.FootprintMarking`, kinds `silkscreen`/`polarity`/`pin_1`) — which feed board visual bounds and clearance diagnostics. See `references/footprints.md` for the SMD, through-hole, outline, and marking recipes.

Structural rule: a footprint reference must resolve to one consistent geometry. The board **rejects same-ref footprint geometry conflicts** — reusing a `(library, name)` ref with pads that match but geometry that differs is an error. Keep one ref → one geometry.

### Phase 6: Build the schematic symbol

`SchematicSymbolSpec` supplies the visual symbol registered with the component definition. Use `.ic()` for rectangular IC blocks or the lower-level constructor for custom primitives. See `references/symbols.md`.

### Phase 7: Set DNP state

```python
component.dnp(False)   # fitted
component.dnp(True)    # do-not-populate
component.dnp()        # dnp() with no arg defaults to True
```

`dnp(bool)` records assembly intent in logical JSON. Call it after `select_part()`; the BOM and CPL exporters read it.

### Phase 8: Verify downstream identity

- Logical JSON `selected_physical_part` must contain: `manufacturer`, `part_number`, `package`, `footprint` (library + name), `pin_pads`, and any `ratings`.
- BOM output must group manufacturer identity, package, DNP state, and sourcing.
- CPL output must contain reference, side, position, rotation, footprint, and part identity for every placed component.

## Validation Checklist

- Run the smallest project or example command that exercises the component.
- Check `Design.validate()`, `Design.validate_for_pcb()`, and `Design.validate_bom_readiness()` when the component must be board/manufacturing ready.
- Inspect generated logical JSON, BOM JSON/CSV, CPL JSON/CSV, PCB JSON, and model asset entries when applicable.
- Add or update project tests for product intent: required nets, no forbidden shorts, board placement, and selected-part expectations.

## References

- `references/footprints.md` — `FootprintDefinition`, `FootprintPad.surface_mount`, `FootprintPad.through_hole`
- `references/symbols.md` — `SchematicSymbolSpec.ic`, `.ic_pin`, `.pin`, `.rectangle`, `.line`, `.text`
- `references/selected-parts.md` — `select_part` patterns for passive, IC, LED, tied lands, bulk loops
- `references/walkthrough-555-components.md` — narrated end-to-end read of the 555 blinker example
- `docs/python-api.md` §"Custom Component Definitions" and §"Selected Physical Parts"
- `docs/logical-circuit-format.md` §"Typed Electrical Attributes", "Component Definitions", "Components", "Reader Validation"
- `docs/design/footprint-library-conventions.html`
- `examples/timer_555_led_blinker/components.py` — primary worked example for this skill
