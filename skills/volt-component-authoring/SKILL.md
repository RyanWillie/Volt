---
name: volt-component-authoring
description: Author or review exact Volt library Parts. Use when defining PinSpec data, electrical semantics, symbols, footprints, pin-to-pad maps, 3D models, BOM/CPL identity, assembly data, or manufacturing identity.
---

# Volt Component Authoring

Use this skill to create or review one complete exact `Part`, from logical pins through
physical manufacturing identity. First read `../shared-volt-architecture.md`.

## Ownership

Reusable parts belong to a versioned `volt.Library`. `Library.part(...)` lowers the complete
definition into the native PartLibraryBundle graph. `Design.instantiate(part, ...)` selects
that exact library PartRef in the logical circuit. Do not define a logical component and
attach physical identity later; there is no supported mutable selection route.

## Define an exact Part

```python
library = volt.Library("acme.power", version="1.0.0")

timer = library.part(
    "NE555DR",
    pins=(
        volt.PinSpec("GND", 1, role="ground"),
        volt.PinSpec("TRIG", 2, role="analog_input"),
        volt.PinSpec("OUT", 3, role="output", signal="digital"),
        volt.PinSpec("RESET", 4, role="input", signal="digital"),
        volt.PinSpec("CTRL", 5, role="analog_input"),
        volt.PinSpec("THRESH", 6, role="analog_input"),
        volt.PinSpec("DISCH", 7, role="analog_output"),
        volt.PinSpec("VCC", 8, role="power"),
    ),
    symbol=TIMER_SYMBOL,
    footprint=TIMER_SOIC_8,
    pads={
        "GND": "1", "TRIG": "2", "OUT": "3", "RESET": "4",
        "CTRL": "5", "THRESH": "6", "DISCH": "7", "VCC": "8",
    },
    manufacturer="Texas Instruments",
    mpn="NE555DR",
    package="SOIC-8",
    prefix="U",
    value="NE555",
    voltage_rating=16.0,
)

design = volt.Design("controller")
u1 = design.instantiate(timer, ref="U1")
```

Exact Parts used in one Design must come from the immutable library closure available when
the first Part is instantiated. Define the complete library before instantiating any member.

## Pins and electrical meaning

`volt.PinSpec(name, number, ...)` defines one stable logical pin:

| Argument | Meaning |
|---|---|
| `name`, `number` | Stable logical name and physical number |
| `role` | Authoring shorthand such as `passive`, `input`, `output`, `power`, or `ground` |
| `requirement` | `required`, `optional`, or `must_not_connect` |
| `signal` | `digital`, `analog`, or `unspecified` |
| `terminal`, `direction`, `drive`, `polarity` | Kernel-owned electrical attributes |
| `voltage_range` | Inclusive `(minimum, maximum)` voltage interval |

For richer exact-part semantics use `ComponentContract` and `ElectricalRecord`. Both lower to
the native library model and are evaluated by kernel-owned ERC.

## Footprint and pin-to-pad map

`FootprintDefinition` holds a library-qualified reference, pads, and optional courtyard,
body, fabrication outline, assembly outline, and semantic markings. `pads=` maps every
stable logical pin name or number to one or more footprint pad labels.

The kernel rejects missing pins, unknown pin keys, missing footprint pads, and conflicting
geometry for one footprint reference. One logical pin may map to multiple physical lands for
a tied-land package.

## Symbol, model, and identity

- `SchematicSymbolSpec` supplies one or more visual variants for the exact Part.
- `PartModel3D` supplies a checked GLB or STEP asset with a footprint-relative transform.
- `manufacturer`, `mpn`, and `package` form exact BOM identity.
- `approved_alternate_mpns` records reviewed sourcing alternates without changing selected
  identity.
- `value` becomes a logical property and can be rendered on the schematic.

## Assembly intent

```python
u1.dnp(False)  # fitted
u1.dnp(True)   # do not populate
```

DNP is instance-level assembly intent stored in the logical circuit. It is independent of
the immutable exact Part definition.

## Validation checklist

- Build the library and require `library.build().ok`.
- Instantiate the Part, then run `Design.validate()`, `validate_for_pcb()`, and
  `validate_bom_readiness()` for board/manufacturing work.
- Confirm the selected PartRef resolves through the Design's PartLibraryBundle closure.
- Inspect BOM identity, CPL identity, PCB footprint geometry, and model assets as relevant.
- Add focused product tests for required nets, forbidden shorts, board placement, DNP, and
  exact part expectations.

## References

- `docs/python-api.md` sections "Part Library Authoring" and "Current Logical Authoring".
- `docs/logical-circuit-format.md` sections "Typed Electrical Attributes", "Component
  Definitions", "Components", and "Reader Validation".
- `docs/design/footprint-library-conventions.html`.
- `docs/design/adr-part-library-identity.md`.
