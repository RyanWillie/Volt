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
- `docs/design/schemdraw-style-schematic-authoring.html` for preferred schematic API design and SchemDraw style conventions.

## Skills In This Folder

Start at `volt-basics` if you are new. The end-to-end flow and the skill for each step:

1. Define parts (pins, electrical semantics, footprint, symbol, selected part) — `volt-component-authoring`.
2. Author logical connectivity (nets, instances, Project, tests) — `volt-circuit-authoring`.
3. Present it as a readable schematic — `volt-schematic-authoring`.
4. Give the board structure and produce the manufacturing handoff — `volt-pcb-authoring`.
5. Place and route the board to a professional standard — `volt-pcb-layout`.

The logical circuit owns connectivity. Schematic and PCB skills project or implement it; they never create, merge, or split nets.

## Validation Posture

- Prefer project-level validation and focused artifact inspection over broad full-suite runs when only authoring docs or example guidance changes.
- When changing examples or generated artifacts, verify deterministic logical JSON, schematic JSON/SVG, PCB JSON/SVG, diagnostics, BOM/CPL, and manufacturing outputs as relevant.
- Treat a clean validation report as necessary but not sufficient for visual surfaces. Inspect rendered schematic and PCB SVGs before calling the result polished.

## Viewing Rendered Output

"Inspect the SVG" means *look at the rendered drawing as an image* and judge it — diagnostics cannot tell you a schematic reads poorly or a board looks cramped. Generate the SVG (`schematic.to_svg()` / `board.to_svg()`, or the `*.svg` / `*.pcb.svg` files from `ProjectResult.write_artifacts(...)`), then view it:

- Most coding agents can view an SVG file directly — open it as you would any image and assess it against the skill's quality rubric.
- If your tooling only renders raster images, rasterize first and view the PNG: `cairosvg sheet.svg -o sheet.png --output-width 1400` (install with `pip install cairosvg`; `qlmanage -t -s 1400 sheet.svg -o .` works on macOS).

This is a strong recommendation, not a hard gate: a clean diagnostics run with an unreviewed drawing is half-finished work.
