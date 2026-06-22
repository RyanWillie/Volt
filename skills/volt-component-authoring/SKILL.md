---
name: volt-component-authoring
description: Author or review Volt component definitions and selected physical parts. Use when defining full Volt components, custom PinSpec data, electrical semantics, selected part metadata, footprints, 3D models, package geometry, BOM/CPL identity, assembly data, or manufacturing identity.
---

# Volt Component Authoring

Use this skill to create or review a complete Volt component record, from logical pins through physical manufacturing identity. First read `../shared-volt-architecture.md`.

## Workflow

1. Define the logical component shape first.
   - Use built-in helpers when they already express the device.
   - For custom parts, use `Design.define_component(...)` and `volt.PinSpec(...)` with stable pin names, physical pin numbers, connection requirements, and fundamental electrical semantics.
   - Keep role presets as authoring shorthand only; durable meaning must lower into kernel-owned pin definitions and typed electrical attributes.

2. Add design-bearing electrical semantics.
   - Prefer typed pin/net/component/selected-part attributes for voltage, current, power, tolerance, polarity, signal, drive, and voltage ranges.
   - Put product-specific intent in explicit properties or project tests when it is not a generic kernel concept.

3. Select the physical implementation per component instance.
   - Include manufacturer, manufacturer part number, package, footprint, logical-pin-to-pad mapping, selected-part properties, and ratings.
   - Ensure every logical pin maps to the intended physical pad labels. Tied lands may map one logical pin to multiple unique pads; missing pins, foreign pins, and duplicate pad labels are structural errors.
   - Set DNP/fitted state and approved alternates through the existing BOM-ready APIs instead of side metadata.

4. Attach board-ready physical data.
   - Provide a `volt.FootprintDefinition` or supported footprint reference with millimeter pad geometry, technology, drill data, mechanical pad roles, and optional package body geometry.
   - Attach 3D model data, offsets, and manifest-indexed model assets only through supported Volt model APIs; do not hide model paths in ad hoc properties.
   - Treat package labels and footprint strings as provenance until they resolve to Volt-owned footprint geometry.

5. Verify downstream identity.
   - Confirm logical JSON contains `selected_physical_part` with manufacturer, MPN, package, footprint, pin-pad mappings, and ratings.
   - Confirm BOM output groups manufacturer identity, package, DNP state, alternates, and sourcing as expected.
   - Confirm CPL output has reference, side, position, rotation, footprint, and part identity for placed board components.

## Validation Checklist

- Run the smallest project or example command that exercises the component.
- Check `Design.validate()`, `Design.validate_for_pcb()`, and `Design.validate_bom_readiness()` when the component must be board/manufacturing ready.
- Inspect generated logical JSON, BOM JSON/CSV, CPL JSON/CSV, PCB JSON, and model asset entries when applicable.
- Add or update project tests for product intent such as required nets, no forbidden shorts, board placement, and selected part expectations.

## References

- `docs/python-api.md` sections "Custom Component Definitions" and "Selected Physical Parts".
- `docs/logical-circuit-format.md` sections "Typed Electrical Attributes", "Component Definitions", "Components", and "Reader Validation".
- `docs/design/footprint-library-conventions.html`.
- `examples/timer_555_led_blinker/components.py`.
- `examples/pcb_led_board/main.py`.
