---
name: volt-pcb-authoring
description: Author or review Volt-native PCB layouts and manufacturing handoffs. Use when creating or checking board placement, stackup, capability profiles, routing, constraints, DRC/manufacturability diagnostics, PCB JSON/SVG artifacts, BOM/CPL, native fabrication reports, or manufacturing package output.
---

# Volt PCB Authoring

Use this skill to create or review Volt PCB projections that physically implement an existing logical circuit and can be handed to manufacturing. First read `../shared-volt-architecture.md`.

## Workflow

1. Confirm board readiness before layout.
   - Verify logical connectivity, selected physical parts, footprint geometry, pin-pad mappings, package data, DNP/fitted state, and BOM readiness.
   - Do not choose parts, footprints, or nets in the PCB layer; consume the selected part and logical net data already owned by the circuit.

2. Define board structure.
   - Create the board projection, outline, mounting/mechanical features, layers, stackup, and design rules or capability-profile snapshot.
   - Keep units in millimeters and use kernel-owned board/layer/feature data, not renderer-only geometry.

3. Place and route over existing connectivity.
   - Place components using resolved footprints and stable board coordinates, side, rotation, and locked state where appropriate.
   - Route tracks, vias, zones, text, and board primitives against existing `NetId` values only.
   - Treat illegal copper, clearance issues, unrouted nets, and manufacturability gaps as DRC/validation findings unless they are structurally invalid references.

4. Validate manufacturing intent.
   - Run project diagnostics, PCB validation, DRC/manufacturability checks, capability lint, BOM readiness, and CPL projection checks as relevant.
   - For KiCad handoff, inspect adapter warnings and `PCB_KICAD_FAB_EXPORT_LOSS`; fab-critical loss must be fixed or explicitly accepted by project policy before manufacturing.
   - For native handoff, use `ProjectResult.write_manufacturing_package(...)` or `volt export manufacturing` so native Gerber/Excellon, BOM, CPL, diagnostics, profile metadata, native fabrication coverage, manifest, inspection HTML, and optional archive are produced together.

5. Review generated artifacts.
   - Inspect PCB JSON for outline, layer stack, placements, footprint definitions, pad resolutions, diagnostics, and net refs.
   - Inspect full PCB SVG and layer SVGs for orientation, pad/net overlays, ratsnest or route coverage, silkscreen legibility, mounting holes, board outline, and diagnostic overlays.
   - Inspect native fabrication report `manufacturing/native-fabrication.json`, package manifest `manufacturing/manifest.json`, profile metadata, BOM/CPL JSON and CSV, Gerber files, drill files, and `manufacturing/inspection.html`.

## Validation Checklist

- Run the smallest project/example generation path that covers the board and require `result.ok`.
- Inspect `diagnostics/diagnostics.json`, `*.volt.pcb.json`, `*.pcb.svg`, layer SVGs when generated, BOM JSON/CSV, CPL JSON/CSV, and manufacturing package output.
- Open the board preview and fabrication files in an appropriate viewer when the result is intended to be orderable.
- Add or update project board tests for outline, placement, expected diagnostics, and product-specific layout constraints.

## References

- `docs/python-api.md` sections "Project Framework" and PCB/manufacturing package APIs.
- `docs/design/pcb-json-format.html`.
- `docs/design/footprint-library-conventions.html`.
- `docs/design/kicad-pcb-export-handoff.html`.
- `examples/pcb_led_board/main.py`.
- `examples/timer_555_led_blinker/board.py`.
- Generated project bundles under `examples/*/artifacts/*.volt/`.
