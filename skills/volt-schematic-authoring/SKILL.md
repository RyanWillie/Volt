---
name: volt-schematic-authoring
description: Author, review, or polish Volt-native schematics over existing logical connectivity. Use when working on schematic placement, symbols, wires, labels, ports, regions, visual readability, rendered SVG inspection, schematic diagnostics, or schematic project tests.
---

# Volt Schematic Authoring

Use this skill to create polished Volt schematics that present existing logical connectivity clearly. First read `../shared-volt-architecture.md`.

## Workflow

1. Verify the logical circuit first.
   - Confirm components, nets, pin membership, selected parts, and intentional no-connects already exist in the design.
   - If a wire would need to connect different logical nets, fix the circuit first; do not patch meaning into the schematic.

2. Lay out the sheet for human reading.
   - Choose sheet size, orientation, title block, margins, coordinate zones, and regions before detailed routing.
   - Group by function: power, MCU/IC core, connectors, clocks, protection, debug, indicators, and other reader-facing blocks.
   - Prefer functional signal flow over raw package pin order when a choice is needed.

3. Place symbols and route presentation objects.
   - Place existing logical components with default or explicit kernel-owned symbols.
   - Draw wires, labels, power/ground ports, off-page ports, junctions, and no-connect markers over existing nets and pins.
   - Use SchemDraw-style sugar for concise presentation, but remember it lowers to durable Volt schematic data.

4. Apply the visual quality bar.
   - Tags and ports must be quiet annotations, not the dominant visual element.
   - Use label/tag restraint: short local labels and stubs for same-sheet handoffs; off-page tags only for real sheet or region boundaries.
   - Keep consistent spacing, aligned tags, readable text, small junction dots, thin wires, and enough whitespace around symbols.
   - Avoid collisions: no label-symbol crowding, text overflow, wire/text overlap, region overflow, or visually ambiguous crossings.
   - Split the design across sheets or use a larger sheet if one page cannot remain readable.

5. Inspect the rendered result.
   - Open the generated SVG, not only the JSON or diagnostics.
   - Confirm the drawing communicates circuit function before implementation detail.
   - Treat clean model/render tests as necessary but not sufficient for professional readability.

## Validation Checklist

- Run the focused project/example generation path and require no unexpected schematic diagnostics.
- Inspect schematic JSON for references to existing logical `ComponentId`, `PinId`, and `NetId` only.
- Inspect rendered SVG for grouping, sheet layout, tag restraint, spacing, collisions, title block fit, and visible coverage of connected/no-connect pins.
- Add project schematic tests for durable contracts such as required component placement or expected diagnostics.

## References

- `docs/schematic-format.md`.
- `docs/python-api.md` section "Schematic Placement".
- `docs/design/schemdraw-style-schematic-authoring.html`.
- `examples/timer_555_led_blinker/schematic.py`.
- `examples/stm32_usb_buck/main.py` plus its `schematic_*.py` region files.
- Existing rendered examples under `examples/*/artifacts/*.svg`.
