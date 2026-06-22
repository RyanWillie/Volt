# Shared Volt Architecture Rules

Read this before applying any official Volt skill in this folder.

## Non-Negotiables

- Invalid kernel state should be impossible; bad circuit design should be diagnosable.
- EDA and manufacturing meaning belongs in the C++ kernel, canonical model, and native exporters.
- Python is an ergonomic authoring and orchestration surface. It must lower meaningful operations into kernel-owned state and must not own hidden EDA semantics.
- The logical circuit owns component definitions, component instances, pins, nets, pin-to-net membership, typed attributes, selected parts, and logical design intent.
- Schematics visualize existing logical connectivity. They may arrange, label, annotate, and validate presentation; they must not create, merge, split, or reinterpret nets.
- PCBs physically implement existing logical connectivity. They may place footprints, route copper, define physical board data, and validate implementation; they must not define the netlist.
- Structural integrity failures belong at mutation/read boundaries. Examples: dangling IDs, duplicate references, invalid selected-part pin-pad mappings, and one pin connected to multiple nets.
- Design-quality failures belong in diagnostics, validation, and project tests. Examples: unconnected required pins, single-pin nets, visual schematic coverage gaps, DRC/manufacturability issues, and BOM/CPL readiness gaps.

## Canonical References

- `AGENTS.md` for the project rules of engagement.
- `docs/architecture.md` for kernel ownership and projection-layer boundaries.
- `docs/authoring-api.md` for C++ authoring facade boundaries and diagnostic flow.
- `docs/python-api.md` for the Python authoring, project, schematic, PCB, and manufacturing package APIs.
- `docs/logical-circuit-format.md` for canonical logical circuit JSON and reader validation.
- `docs/schematic-format.md` for schematic projection JSON and SVG semantics.
- `docs/design/footprint-library-conventions.html` for footprint geometry and selected-part boundaries.
- `docs/design/pcb-json-format.html` for PCB JSON, viewer diagnostics, SVG selectors, DRC, and capability lint boundaries.
- `docs/design/kicad-pcb-export-handoff.html` for KiCad adapter review and fab-critical loss handling.

## Validation Posture

- Prefer project-level validation and focused artifact inspection over broad full-suite runs when only authoring docs or example guidance changes.
- When changing examples or generated artifacts, verify deterministic logical JSON, schematic JSON/SVG, PCB JSON/SVG, diagnostics, BOM/CPL, and manufacturing outputs as relevant.
- Treat a clean validation report as necessary but not sufficient for visual surfaces. Inspect rendered schematic and PCB SVGs before calling the result polished.
