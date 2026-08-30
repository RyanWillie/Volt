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
- [`adr-part-semantics-and-identity.md`](adr-part-semantics-and-identity.md) — freeze
  component contracts, exact-part identity, integrity-bearing selection, and canonical
  v1 Voltage/Current semantics
- [`adr-projection-ownership-and-compiled-board.md`](adr-projection-ownership-and-compiled-board.md)
  — freeze projection ownership, named Boards, and the immutable `CompiledBoard` contract
- [`adr-project-bundle-artifact-graph.md`](adr-project-bundle-artifact-graph.md) — ProjectBundle
  v2 typed artifact graph, dependency lock, safe native reopening, and opt-in exports
- [`volt-post-circuit-architecture-review.html`](volt-post-circuit-architecture-review.html)
  — approved owner-aligned Schematic/Board, `CompiledBoard`, artifact graph, bundle and
  project-tooling direction; focused ADRs freeze exact implementation contracts

## Design notes and explorations

- [`adr-part-electrical-model.md`](adr-part-electrical-model.md) — proposed E0 contract for
  exact-Part R/C/L composition, current-only persistence, typed DC testbench and read-only
  compilation; [single-page companion](part-electrical-model-contract.html)

- `circuit-aggregate-api.html` — single-page review companion for the accepted Circuit API
  ADR and migration roadmap
- `circuit-semantic-parity.html` — final evidence matrix for Circuit API redesign parity,
  atomicity, persistence, current-format policy, and downstream behavior
- `architecture-m2-deletion-parity.html` — issue #321 deletion metrics, surviving graph,
  retained v1 boundary, and zero-state anti-regrowth evidence

- `hierarchy-scoped-net-design.html` — exported mirror of
  [`../hierarchy-scoped-net-design.md`](../hierarchy-scoped-net-design.md)
- `schematic-architecture-plan.html` — schematic projection architecture plan
- `schemdraw-style-schematic-authoring.html` — SchemDraw-style schematic authoring exploration
- `kicad-schematic-adapter-design.html` — KiCad schematic adapter design
- `kicad-pcb-export-handoff.html` — KiCad PCB export handoff notes
- `simulation-feature-guide-2026-05-28.html` — simulation feature guide

## Dated implementation plans

- `kernel-compiled-libraries-refactor-plan-2026-06-01.html` — compiled-libraries refactor plan
