# Design and planning artifacts

This directory holds standalone HTML design notes, data-contract references, and dated
planning documents that were exported as self-contained pages.

Most are kept for history and rationale rather than as the canonical documentation surface.
Living documentation normally lives as Markdown in [`docs/`](../) (and is consumed by
Doxygen, which ingests `*.md` and `*.hpp` only) and as the Mintlify site under
[`docs-site/`](../../docs-site). This Markdown index makes the artifacts discoverable, but
the HTML exports themselves are not part of any generated output. The two maintainer-approved
architecture references listed below are deliberate exceptions and remain authoritative for
program direction until their decisions are superseded by focused ADRs.

When the content of another note becomes a stable, maintained contract, prefer promoting it
into a Markdown document under `docs/` rather than editing the exported HTML.

## Data-contract and convention references

- `diagnostic-codes.html` — Volt diagnostic code catalog
- `footprint-library-conventions.html` — footprint library naming and structure conventions
- `pcb-json-format.html` — PCB projection JSON data contract

## Accepted architecture decisions

- [`adr-append-only-kernel.md`](adr-append-only-kernel.md) — kernel models are append-only
  compiled build artifacts
- [`adr-circuit-aggregate-api.md`](adr-circuit-aggregate-api.md) — replace storage-shaped
  Circuit facades with a small typed aggregate API
- [`adr-projection-ownership-and-compiled-board.md`](adr-projection-ownership-and-compiled-board.md)
  — freeze projection ownership, named Boards, and the immutable `CompiledBoard` contract
- [`volt-post-circuit-architecture-review.html`](volt-post-circuit-architecture-review.html)
  — approved owner-aligned Schematic/Board, `CompiledBoard`, artifact graph, bundle and
  project-tooling direction; focused ADRs freeze exact implementation contracts
- [`volt-part-library-architecture-review.html`](volt-part-library-architecture-review.html)
  — approved component/part identity, canonical Voltage/Current semantics, native library
  bundle and exact-selection direction; focused ADRs freeze exact implementation contracts

## Design notes and explorations

- `circuit-aggregate-api.html` — single-page review companion for the accepted Circuit API
  ADR and migration roadmap
- `circuit-semantic-parity.html` — final evidence matrix for Circuit API redesign parity,
  atomicity, persistence, compatibility, and downstream behavior

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
