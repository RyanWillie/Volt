# Official Volt Skills — Design

Date: 2026-06-22
Status: Approved design, pending spec review

## Problem

A fresh agent that does not know Volt or its Python API cannot currently learn the
end-to-end flow (component → circuit → schematic → PCB → manufacturing) from the skills
that exist. There are two problems:

1. **The `skills/` authoring skills are thin policy.** The four skills
   (`volt-component-authoring`, `volt-circuit-authoring`, `volt-schematic-authoring`,
   `volt-pcb-authoring`) state architectural boundaries and point at docs, but contain
   almost no concrete, copy-pasteable API. An agent must read ~2,100 lines of docs across
   six files before writing a line of code.

2. **Two skill folders with different contents.** `skills/` holds the four authoring
   skills. `.agents/skills/` holds seven Scrum/process ceremonies plus one rich, hands-on
   authoring skill (`volt-pcb-layout`). The Scrum skills are no longer wanted.

This project makes `skills/` the single comprehensive source of truth for **authoring**
skills, written so a fresh agent can do real work, and removes the Scrum skills.

## Goals

- A fresh agent can go from zero to a manufacturable board using only `skills/`.
- Each skill teaches the **real, current, preferred** API with correct copy-paste code,
  grounded in the worked examples so it cannot drift into invented method names.
- The schematic and PCB layout skills lead with the **preferred relative/fluent APIs**,
  not the lower-level coordinate APIs.
- Architectural boundaries (kernel owns EDA meaning; projections never define the netlist)
  are preserved as the "rules" layer on top of concrete teaching.

## Non-Goals

- No changes to the Volt Python/C++ API itself. This is documentation/skill work only.
- No new examples or generated artifacts. The skills reference the existing examples.
- The Scrum/process ceremonies are not migrated or rewritten; they are removed.
- No simulation, ERC-deepening, or roadmap features are documented beyond what ships today.

## Final Structure

`skills/` becomes the single source of truth:

```
skills/
  shared-volt-architecture.md        # keep, lightly refresh — the rules/boundaries layer
  volt-basics/                       # NEW — orientation + "when to use which skill" router
    SKILL.md
    references/                      # one minimal end-to-end hello-world
  volt-component-authoring/
    SKILL.md
    references/
  volt-circuit-authoring/
    SKILL.md
    references/
  volt-schematic-authoring/
    SKILL.md
    references/
  volt-pcb-authoring/
    SKILL.md
    references/
  volt-pcb-layout/                   # MOVED from .agents/skills, then aligned
    SKILL.md
    references/
```

`.agents/skills/` is deleted in full. `volt-pcb-layout` is preserved by moving it into
`skills/`. The seven Scrum skills (`volt-backlog-refinement`, `volt-pr`, `volt-task`,
`volt-retrospective`, `volt-sprint-kickoff`, `volt-sprint-planning`, `volt-sprint-review`)
are removed.

### Migration safety

Before deleting `.agents/skills/`, grep the repo for references to that path (notably
`AGENTS.md`, `.agents/`, CI configs, and any tooling that enumerates skills) and update or
remove them. The `volt-retrospective` skill referenced `.agents/skills/*` as an improvement
target; any such guidance that should persist moves to `shared-volt-architecture.md` or is
dropped with the Scrum skills.

## Skill Shape (progressive disclosure)

Every skill is a focused `SKILL.md` plus a `references/` folder.

- **SKILL.md** (~120–250 lines): the `name`/`description` frontmatter (description written
  so the skill triggers on the right requests), workflow phases, the architectural rules
  that apply, a quality rubric where relevant, an anti-patterns section, and pointers into
  `references/` and the real `examples/`.
- **references/**: the deep copy-paste layer — a cookbook of correct API snippets, API
  cheat-sheet tables, and an annotated walkthrough of the canonical example. Snippets are
  drawn from the real `examples/` and `python/volt/` source so they are verified-correct,
  and they link back to the example files to limit drift.

`volt-basics` is the exception: a single SKILL.md plus one short end-to-end hello-world
reference is enough; it is an on-ramp, not a deep reference.

## Source-of-Truth Discipline

To keep the skills accurate and prevent drift:

- Code snippets are copied or adapted from real, working sources: the `examples/` trees
  (`timer_555_led_blinker`, `stm32_usb_buck`, `pcb_led_board`, `schematic_sugar`) and the
  `python/volt/` modules. No method or argument is documented unless it exists in source.
- Each `references/` file names the example file it derives from so a reader can open the
  full, runnable version.
- The canonical docs (`docs/python-api.md`, `docs/logical-circuit-format.md`,
  `docs/schematic-format.md`, `docs/design/*.html`) remain the authoritative specification;
  skills teach how to *use* the API and link to docs for the full contract.
- During implementation, every non-trivial snippet is validated against source before it is
  written into a skill (read the module or run the smallest example path).

## Per-Skill Content

### volt-basics (NEW — fresh-agent entry point)

Purpose: orient an agent who has never used Volt, and route them to the right skill.

SKILL.md covers:

- `import volt`; the difference between a loose `volt.Design` script and a `volt.Project`
  product workflow, and when to use each.
- The three project stages and their order: `@project.design` → `@project.schematic` →
  `@project.board`; stages return models and optional `volt.ProjectResource` handles;
  later stages receive a `volt.BuildContext` (`context.design()`, `context.resource(...)`).
- The full lifecycle and where artifacts land: `project.run()` → `ProjectResult` →
  `result.write(path)` (bundle) and `result.write_artifacts(...)` (SVGs etc.) →
  `result.write_manufacturing_package(...)` (orderable handoff).
- A **decision table**: "I want to … → use skill X". (Define a part / its footprint / its
  symbol → component-authoring. Wire up connectivity / nets / a Project / tests →
  circuit-authoring. Draw a readable schematic → schematic-authoring. Set up board
  structure / produce Gerbers/BOM/CPL → pcb-authoring. Place and route the board nicely →
  pcb-layout.)
- Validation posture: `result.ok`, diagnostics vs raised exceptions, "clean validation is
  necessary but not sufficient for visual surfaces — look at the SVG."

references/: one minimal end-to-end LED example (design → schematic via `drawing()` →
board via `board.layout()` → `write_artifacts`), heavily commented, derived from the
`schematic_sugar` and `pcb_led_board` examples.

### volt-component-authoring

SKILL.md covers the lifecycle of a complete component record:

- Built-in catalog helpers first (`design.R`, `design.C`, `design.LED`,
  `design.connector_1x02`, …) — when they already express the device.
- Custom logical shape: `design.define_component(name, pins=[volt.PinSpec(...)], ...)`;
  `PinSpec` roles (`power`, `power_input`/`power_output`, `ground`, `input`/`output`,
  `analog_input`/`analog_output`, `bidirectional`, `no_connect`) and connection
  requirements (`required`/`optional`/`must_not_connect`); typed electrical semantics
  (`terminal`, `direction`, `signal`, `drive`, `polarity`, `voltage_range`) and that they
  lower into kernel JSON and feed ERC.
- Selected physical part: `component.select_part(manufacturer, part_number, package,
  footprint, pin_pads, properties, voltage_rating, power_rating)`; `pin_pads` keyed by pin
  number or pin name; tied lands mapping one logical pin to multiple pads; structural
  errors (missing/unknown pins, duplicate pads). `component.dnp(bool)` and approved
  alternates through BOM-ready APIs, not side metadata.
- Footprint geometry: `volt.FootprintDefinition((library, name), pads=(...))` with
  `volt.FootprintPad.surface_mount(label, at, size, shape, mechanical_role)` (and
  through-hole/drill variants), millimeter units, mechanical pad roles, package body.
- Schematic symbol: `volt.SchematicSymbolSpec` / `SchematicSymbolSpec.ic` /
  `ic_pin(name, number, side, slot, label)` / `pin` / primitives (`rectangle`, `line`,
  `text`); attaching via `schematic_symbol=` on `define_component`.
- 3D model attachment through supported model APIs (no ad-hoc property paths).
- Downstream identity: confirm `selected_physical_part` in logical JSON; BOM grouping
  (manufacturer, package, DNP, alternates, sourcing); CPL fields (ref, side, position,
  rotation, footprint, identity).

references/: footprint cookbook, `SchematicSymbolSpec` cookbook, `select_part` patterns
(incl. tied lands), and an annotated walkthrough of
`examples/timer_555_led_blinker/components.py`.

### volt-circuit-authoring

SKILL.md covers building canonical logical connectivity:

- `volt.Design(name)` root; nets via `design.net(name, kind=..., voltage=...)` with kinds
  `power`/`ground`/signal; component instances via catalog helpers or
  `design.instantiate(definition, ref=..., properties=...)`.
- Connectivity through the kernel only: `net += pin`, `net += (pinA, pinB, …)`; pin access
  by number (`r1[1]`) and by name (`d1["A"]`, `u1["VCC"]`); the rule that schematic wires
  and PCB copper never define connectivity.
- Typed electrical/power intent: net `voltage`, `PinSpec` semantics, net classes
  (`design.net_class(...)`), selected-part ratings.
- `volt.Project` framework: stage decorators, returning
  `(design, volt.ProjectResource("nets", nets), volt.ProjectResource("parts", parts))`;
  reaching them later via `context.design()` / `context.resource("nets", dict)`;
  `project.run()`, `project.run_through(project.design)` for iteration.
- Project tests (`@project.design.test`, `.schematic.test`, `.board.test`): `check.net(...)
  .connects(...)`, `check.no_connection(...)`, `check.places(...)`, `check.has_outline()`;
  multi-model `check.names()`/`check.designs()`/etc.; expected diagnostics for intentional
  findings.
- Diagnostics vs exceptions: structural errors raise; design-quality issues are
  diagnostics. Deterministic artifacts via the project bundle.

references/: connectivity/operator cheat-sheet, Project lifecycle cookbook, project-test
patterns, annotated `examples/timer_555_led_blinker/main.py` + `project_tests.py`, and a
pointer to `examples/stm32_usb_buck/` for a multi-file project.

### volt-schematic-authoring

Leads with the **preferred `drawing()` sugar**; the low-level placement API is documented
as the fallback.

SKILL.md covers:

- Precondition: connectivity already exists in the logical circuit; the schematic only
  presents it.
- Sheet setup: `design.schematic(name, size=, orientation=, title=, number=, page_count=,
  revision=, date=, project=, file=, margins=, coordinate_zones=, grid=)`.
- **Preferred path — the drawing session**: `with sheet.drawing(at=, unit=) as drawing:`
  - `drawing.place(part, at=, orient=)` and the fluent
    `drawing.two_terminal(part).at(anchor).anchor(ref).drop(ref).between(a,b)
    .to()/.tox()/.toy().length().right()/left()/up()/down().reverse().flip()
    .label_ref()/.label_value()/.label().dot()/.idot()`.
  - `drawing.connect(a, b, shape=...)`, `drawing.wire(net).at(...).tox(...).direct()`,
    `drawing.power_stub`/`ground_stub`/`power`/`ground`, `drawing.local_label`/`net_label`,
    `drawing.signal_tag(s)`/`signal_stub(s)`, `drawing.junction`, `drawing.no_connect`,
    `drawing.sheet_port`/`off_page`.
  - Relative cursor tools: anchors with `.up()/.down()/.left()/.right()/.tox()/.toy()`,
    `drawing.hold()`, `drawing.push()/pop()`, `drawing.move()/move_from()`, `drawing.stack`,
    `drawing.frame`, `drawing.unit`, `drawing.here`, `drawing.direction`.
- **Fallback path — low-level placement**: `sch.place(...)`, `sch.wire(net).from_().via()
  .to().orthogonal()`, `sch.power/ground/label/junction/sheet_port/off_page/no_connect` —
  for cases the drawing session does not cover.
- Layout for human reading: group by function (power, IC core, connectors, clocks,
  protection, debug, indicators); functional signal flow over package pin order.
- **Visual quality rubric**: tag/port restraint, consistent spacing, aligned labels, thin
  wires, small junction dots, whitespace, no collisions/overflow/ambiguous crossings; split
  sheets when a page can't stay readable.
- Inspection: render and **open the SVG**; confirm function reads before detail; clean
  model/render tests are necessary but not sufficient.

references/: full `two_terminal` fluent-builder reference, drawing-session toolbox, sheet/
title-block option reference, low-level API reference, annotated
`examples/timer_555_led_blinker/schematic.py` and `examples/schematic_sugar/*`.

### volt-pcb-authoring (structure + manufacturing handoff)

Scope: board structure and the manufacturing handoff. Placement/routing craft lives in
`volt-pcb-layout`; the two cross-link.

SKILL.md covers:

- Board-readiness gate before structure: `design.validate_for_pcb()`, selected parts,
  resolved footprints, pin-pad maps, BOM readiness.
- Board structure: `design.board(name)`; `board.add_layer(name, role=, side=)`;
  `board.set_layer_stack((front, back), thickness=)`; `board.set_design_rules(
  copper_clearance, min_track_width, min_via_drill, min_via_annular,
  board_outline_clearance)`; `board.set_rectangular_outline(origin=, size=)`;
  `board.add(volt.Hole(center=, diameter=, role=))`; capability profile snapshot.
- Manufacturing handoff: `result.write_manufacturing_package(path, board=,
  manufacturing_profile=, archive=)` and `volt export manufacturing`; the produced set
  (native Gerber/Excellon, BOM, CPL, diagnostics, profile metadata, native fabrication
  coverage, manifest, `inspection.html`, optional zip); `volt.ManufacturingPackageError`
  when the result is not ok or fab-critical loss is reported.
- KiCad handoff: adapter warnings and `PCB_KICAD_FAB_EXPORT_LOSS`; fab-critical loss must
  be fixed or explicitly accepted before manufacturing.
- Validation and artifact review: PCB JSON, full + layer SVGs, BOM/CPL JSON+CSV,
  `manufacturing/native-fabrication.json`, `manifest.json`, `inspection.html`.

references/: board-setup cookbook, manufacturing-package walkthrough, annotated
`examples/timer_555_led_blinker/board.py` (structure portions) and
`examples/pcb_led_board/main.py`.

### volt-pcb-layout (placement + routing craft — moved & aligned)

Keep the existing rich content (7-category rubric, phased workflow, anti-patterns). Align
to the current API and the new neighbours. Lead with the **relative grammar**.

Updates from the existing skill:

- Add the core principle explicitly: **place the few anchor components (central IC,
  connectors, mounting refs) with explicit coordinates and `locked=True`, then derive
  everything else relatively**; absolute tuples are the escape hatch — `snap()` anything
  reused.
- Document the fuller relative toolset now available:
  `layout.place`, `layout.two_pad(...).at().anchor().drop().right()/left()/up()/down()`,
  `layout.align`, `layout.distribute`, `layout.stack(count=, pitch=)`, `layout.mirror`,
  `layout.move`/`move_from`, `layout.hold`, `layout.frame`, `layout.bundle`,
  `layout.fanout`, `layout.stitch`, `layout.via`, `layout.zone`, `layout.keepout`,
  `layout.rect`/`polygon`, `layout.text`, `layout.node`, `layout.snap`/`snap_x`/`snap_y`,
  `layout.rule`; the `route(net, layer=, width=).at().through().to().tox().toy()
  .left()/right()/up()/down()` builder; anchor relative helpers.
- Cross-link to `volt-pcb-authoring` for board structure and manufacturing output.
- Add `references/`: full `board.layout()` grammar reference and annotated routing from
  `examples/timer_555_led_blinker/board.py`.

## shared-volt-architecture.md

Keep as the rules/boundaries layer. Light refresh: confirm doc paths still resolve, add a
one-line map of the five skills and the end-to-end flow so the boundaries connect to the
new on-ramp, and absorb any durable guidance worth keeping from the removed Scrum skills
(e.g. the kernel-owns-EDA-semantics rule, which is already present).

## Implementation Order

1. Verify and capture the real API surface from `python/volt/` and the examples (largely
   done during design; re-confirm each snippet at write time).
2. Move `volt-pcb-layout` into `skills/`; grep for and update `.agents/skills` references;
   delete `.agents/skills/`.
3. Refresh `shared-volt-architecture.md`.
4. Write `volt-basics` (SKILL.md + hello-world reference).
5. Rewrite the four authoring skills (SKILL.md + references/), one at a time, each
   validated against source/examples before moving on.
6. Align `volt-pcb-layout` (relative-first principle, fuller toolset, references/).
7. Final pass: cross-links resolve, descriptions trigger correctly, every snippet traces to
   a real source.

## Verification

- Every documented method/argument exists in `python/volt/` or the examples.
- Each `references/` snippet either matches an example file or is validated by reading the
  implementing module.
- `skills/` contains six entries (`shared-volt-architecture.md` + five skill folders) and
  `.agents/skills/` no longer exists; no dangling repo references to the old path.
- A skim test: following `volt-basics` → the four authoring skills end to end describes a
  complete path from component definition to manufacturing package without needing to read
  the docs first.
```
