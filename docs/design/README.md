# Design and planning artifacts

This directory holds standalone HTML design notes, data-contract references, and dated
planning documents that were exported as self-contained pages.

They are kept for history and rationale, but they are **not** the canonical documentation
surface. Living documentation lives as Markdown in [`docs/`](../) (and is consumed by
Doxygen, which ingests `*.md` and `*.hpp` only) and as the Mintlify site under
[`docs-site/`](../../docs-site). These HTML files are not referenced by either surface and
are not part of any generated output.

When the content of one of these notes becomes a stable, maintained contract, prefer
promoting it into a Markdown document under `docs/` rather than editing the exported HTML.

## Data-contract and convention references

- `diagnostic-codes.html` — Volt diagnostic code catalog
- `footprint-library-conventions.html` — footprint library naming and structure conventions
- `pcb-json-format.html` — PCB projection JSON data contract

## Design notes and explorations

- `hierarchy-scoped-net-design.html` — exported mirror of
  [`../hierarchy-scoped-net-design.md`](../hierarchy-scoped-net-design.md)
- `schematic-architecture-plan.html` — schematic projection architecture plan
- `schemdraw-style-schematic-authoring.html` — SchemDraw-style schematic authoring exploration
- `kicad-schematic-adapter-design.html` — KiCad schematic adapter design
- `kicad-pcb-export-handoff.html` — KiCad PCB export handoff notes
- `simulation-feature-guide-2026-05-28.html` — simulation feature guide
- `volt-project-framework-spec-2026-06-02.html` — Volt project framework spec
- `volt-project-framework-feature-guide-2026-06-03.html` — Volt project framework feature guide

## Dated implementation plans

- `pcb-development-plan-2026-05-29.html` — PCB development plan
- `kernel-compiled-libraries-refactor-plan-2026-06-01.html` — compiled-libraries refactor plan
- `testing-overhaul-plan.html` — testing overhaul baseline review
