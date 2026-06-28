# Official Volt Skills Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `skills/` the single, comprehensive source of truth for authoring Volt — six skills that take a fresh agent from a component definition to a manufacturable board — and delete the unwanted Scrum skills.

**Architecture:** Each skill is a focused `SKILL.md` plus a `references/` cookbook. Skills teach the *real, current, preferred* API (schematic `drawing()` sugar; relative `board.layout()` grammar), with every snippet traceable to `python/volt/` source or an `examples/` file. `shared-volt-architecture.md` remains the boundaries layer.

**Tech Stack:** Markdown skills (YAML frontmatter + body); the Volt Python package under `python/volt/`; worked examples under `examples/`; canonical docs under `docs/`.

## Global Constraints

- Skills live under `skills/`; `.agents/skills/` is deleted entirely.
- Final `skills/` contents: `shared-volt-architecture.md` + five skill folders: `volt-basics`, `volt-component-authoring`, `volt-circuit-authoring`, `volt-schematic-authoring`, `volt-pcb-authoring`, `volt-pcb-layout` (six folders total — `volt-pcb-authoring` and `volt-pcb-layout` are separate).
- Every method, argument, or class a skill documents MUST exist in `python/volt/` or an `examples/` file. No invented API. Re-confirm at write time by reading the module or example.
- Code snippets are copied/adapted from real sources; each `references/` file names the `examples/` file it derives from.
- Schematic skill leads with the `drawing()` session API; low-level `sch.place()/sch.wire()` is documented only as the fallback.
- PCB-layout skill leads with the relative `board.layout()` grammar; the core principle is "place anchor parts with explicit coords + `locked=True`, derive the rest relatively; `snap()` anything reused; absolute tuples are the escape hatch."
- SKILL.md `description:` frontmatter must be written to trigger on the right requests (state when to use the skill).
- Commit after each task. End commit messages with the Co-Authored-By trailer used in this repo.
- Spec: `docs/superpowers/specs/2026-06-22-official-volt-skills-design.md` — the authority for content; this plan operationalizes it.

---

### Task 1: Migrate folders — move volt-pcb-layout into skills/, delete .agents/skills

**Files:**
- Move: `.agents/skills/volt-pcb-layout/` → `skills/volt-pcb-layout/`
- Delete: `.agents/skills/` (all seven Scrum skills)

**Interfaces:**
- Produces: `skills/volt-pcb-layout/SKILL.md` (relocated, unchanged content for now — aligned in Task 8).

- [ ] **Step 1: Confirm no repo references to `.agents/skills` outside the dir**

Run: `grep -rn "agents/skills" --include="*.md" --include="*.json" --include="*.yml" --include="*.yaml" --include="*.toml" --include="*.py" . | grep -v "^\./\.agents/skills/" | grep -v "docs/superpowers/specs/2026-06-22-official-volt-skills-design.md"`
Expected: no output (only the spec mentions the path, which is fine).

- [ ] **Step 2: Move volt-pcb-layout into skills/ with git**

Run: `git mv .agents/skills/volt-pcb-layout skills/volt-pcb-layout`
Expected: no error; `skills/volt-pcb-layout/SKILL.md` exists.

- [ ] **Step 3: Delete the remaining .agents/skills tree**

Run: `git rm -r .agents/skills`
Expected: seven Scrum skill files staged for deletion; `.agents/skills` no longer exists.

- [ ] **Step 4: Verify the resulting tree**

Run: `ls skills/ && test ! -d .agents/skills && echo "OK: .agents/skills gone"`
Expected: lists `shared-volt-architecture.md`, `volt-circuit-authoring`, `volt-component-authoring`, `volt-pcb-authoring`, `volt-pcb-layout`, `volt-schematic-authoring`, and prints `OK: .agents/skills gone`. (`volt-basics` is added in Task 3.)

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "Move volt-pcb-layout into skills/, remove Scrum skills

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: Refresh shared-volt-architecture.md

**Files:**
- Modify: `skills/shared-volt-architecture.md`

**Interfaces:**
- Produces: a refreshed boundaries doc that adds a flow map + skill index the other skills link to ("First read `../shared-volt-architecture.md`").

- [ ] **Step 1: Verify every doc path it references still resolves**

Run: `for p in docs/architecture.md docs/authoring-api.md docs/python-api.md docs/logical-circuit-format.md docs/schematic-format.md docs/design/footprint-library-conventions.html docs/design/pcb-json-format.html docs/design/kicad-pcb-export-handoff.html docs/design/schemdraw-style-schematic-authoring.html; do test -f "$p" && echo "OK $p" || echo "MISSING $p"; done`
Expected: all `OK`. If any `MISSING`, fix the path in the doc to the real file.

- [ ] **Step 2: Add a "Skills In This Folder" section**

Add a short section listing the six skills and the end-to-end flow, so the boundaries connect to the on-ramp. Content to add (verbatim intent, adapt prose):

```markdown
## Skills In This Folder

Start at `volt-basics` if you are new. The end-to-end flow and the skill for each step:

1. Define parts (pins, electrical semantics, footprint, symbol, selected part) — `volt-component-authoring`.
2. Author logical connectivity (nets, instances, Project, tests) — `volt-circuit-authoring`.
3. Present it as a readable schematic — `volt-schematic-authoring`.
4. Give the board structure and produce the manufacturing handoff — `volt-pcb-authoring`.
5. Place and route the board to a professional standard — `volt-pcb-layout`.

The logical circuit owns connectivity. Schematic and PCB skills project or implement it; they never create, merge, or split nets.
```

- [ ] **Step 3: Add the schemdraw design doc to Canonical References**

In the "Canonical References" list, add a line for `docs/design/schemdraw-style-schematic-authoring.html` (the preferred schematic API design doc) if not already present.

- [ ] **Step 4: Skim-review**

Read the file top to bottom. Confirm: no dangling paths, the flow map matches the six skills, the non-negotiables are intact.

- [ ] **Step 5: Commit**

```bash
git add skills/shared-volt-architecture.md
git commit -m "Refresh shared Volt architecture with skill flow map

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: Write volt-basics (orientation + router)

**Files:**
- Create: `skills/volt-basics/SKILL.md`
- Create: `skills/volt-basics/references/hello-world.md`

**Interfaces:**
- Consumes: the lifecycle facts in `docs/python-api.md` §"Project Framework"; examples `schematic_sugar/` and `pcb_led_board/`.
- Produces: the fresh-agent entry point that all other skills can assume was read.

- [ ] **Step 1: Confirm the lifecycle API surface exists**

Run: `grep -nE "def (run|run_through|write|write_artifacts|write_manufacturing_package|design|schematic|board|resource)" python/volt/project.py | head -40`
Expected: shows `run`, `write`, `write_artifacts`, `write_manufacturing_package`, stage decorators, and `resource`. Use only names that appear.

- [ ] **Step 2: Write SKILL.md**

`description:` must trigger on "getting started with Volt / which Volt skill / how do I use Volt / Volt project structure". Body sections:
- `import volt`; `volt.Design` (loose script) vs `volt.Project` (product workflow) and when to use each.
- The three stages in order (`@project.design` → `@project.schematic` → `@project.board`), that stages may return `(model, volt.ProjectResource("nets", nets), ...)`, and that later stages get a `volt.BuildContext` (`context.design()`, `context.resource("nets", dict)`).
- Lifecycle + artifacts: `project.run()` → `ProjectResult`; `result.ok`; `result.write(path)` (bundle: logical JSON, schematic JSON/SVG, PCB JSON/SVG, diagnostics, tests, manifest); `result.write_artifacts(...)`; `result.write_manufacturing_package(...)`.
- A **decision table** mapping intent → skill (component / circuit / schematic / pcb-authoring / pcb-layout), per the spec.
- Validation posture: diagnostics vs raised exceptions; "clean validation is necessary but not sufficient for visual surfaces — open the SVG."
- Pointer: "First read `../shared-volt-architecture.md`."

- [ ] **Step 3: Write references/hello-world.md**

A single minimal end-to-end example: a one-LED design (net + `design.R` + `design.LED` + `design.connector_1x02`, connectivity via `net += pins`), a schematic built with `with sheet.drawing(...) as drawing:` sugar, a board built with `with board.layout(...) as layout:`, then `result.write_artifacts(...)`. Adapt the real code from `examples/schematic_sugar/compact_led.py` and `examples/pcb_led_board/main.py`. Comment each block. Name both source files at the top.

- [ ] **Step 4: Verify snippet provenance**

Run: `python - <<'PY'
import re, pathlib
text = pathlib.Path("skills/volt-basics/references/hello-world.md").read_text()
calls = sorted(set(re.findall(r"\b(?:volt|design|d|project|result|sheet|drawing|board|layout)\.([a-z_]+)\(", text)))
src = "\n".join(p.read_text() for p in pathlib.Path("python/volt").glob("*.py"))
ex = "\n".join(p.read_text() for p in pathlib.Path("examples").rglob("*.py"))
missing = [c for c in calls if (f"def {c}" not in src) and (f".{c}(" not in ex)]
print("MISSING:", missing or "none")
PY`
Expected: `MISSING: none`. Fix any method that does not resolve to source or an example.

- [ ] **Step 5: Commit**

```bash
git add skills/volt-basics
git commit -m "Add volt-basics orientation skill

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: Rewrite volt-component-authoring

**Files:**
- Modify: `skills/volt-component-authoring/SKILL.md`
- Create: `skills/volt-component-authoring/references/footprints.md`
- Create: `skills/volt-component-authoring/references/symbols.md`
- Create: `skills/volt-component-authoring/references/selected-parts.md`
- Create: `skills/volt-component-authoring/references/walkthrough-555-components.md`

**Interfaces:**
- Consumes: `docs/python-api.md` §"Custom Component Definitions"/"Selected Physical Parts"; `examples/timer_555_led_blinker/components.py`.
- Produces: the component-record skill that circuit-authoring and pcb-authoring assume.

- [ ] **Step 1: Confirm the component API surface**

Run: `grep -nE "def (define_component|instantiate|select_part|dnp|R|C|LED|connector_1x02)" python/volt/design.py python/volt/logical.py python/volt/part.py 2>/dev/null | head -40 && echo "--- pad ctors ---" && grep -nE "def (surface_mount|through_hole)" python/volt/pcb.py && echo "--- symbol spec ---" && grep -nE "def (ic|ic_pin|pin|rectangle|line|text)" python/volt/_library_symbol_builders.py python/volt/library.py 2>/dev/null | head`
Expected: `define_component`, `instantiate`, `select_part`, `dnp`, catalog helpers, `FootprintPad.surface_mount/through_hole`, and `SchematicSymbolSpec` builders all resolve. Use only what appears.

- [ ] **Step 2: Rewrite SKILL.md**

`description:` triggers on "define a Volt component / footprint / 3D model / pins / symbol / selected part / BOM identity". Body (workflow phases): built-in catalog first; custom shape via `define_component(name, pins=[volt.PinSpec("NAME", number, role=..., signal=..., voltage_range=...)], source=..., properties=..., schematic_symbol=...)` with the role + connection-requirement + electrical-semantics list from the spec; `select_part(manufacturer, part_number, package, footprint, pin_pads, properties, voltage_rating, power_rating)` incl. pin-name vs pin-number keys, tied lands, structural errors; `dnp(bool)` and alternates; footprint + symbol attachment (point into the references/); downstream identity checks (logical JSON `selected_physical_part`, BOM, CPL). Keep the existing Validation Checklist, updated. Open with "First read `../shared-volt-architecture.md`."

- [ ] **Step 3: Write references/footprints.md**

Cookbook for `volt.FootprintDefinition((library, name), pads=(...))`, `volt.FootprintPad.surface_mount(label, at=, size=, shape=, mechanical_role=)` and `volt.FootprintPad.through_hole(...)`, mm units, mechanical roles. Quote the `_front_smd_pad` helper and `FOOTPRINTS` dict from `examples/timer_555_led_blinker/components.py`. Name that source file.

- [ ] **Step 4: Write references/symbols.md**

Cookbook for `volt.SchematicSymbolSpec`, `.ic(id, pins=(...), center_label=, pin_numbers=)`, `.ic_pin(name, number, side=, slot=, label=)`, `.pin(name, number, pos, orient)`, primitives `.rectangle/.line/.text`. Quote `TIMER_SYMBOL` and `SUPPLY_SYMBOL` from `components.py`. Name the source.

- [ ] **Step 5: Write references/selected-parts.md**

`select_part` patterns: passive (pin-number pads), IC (pin-name pads), tied lands (one pin → multiple pads), ratings (`voltage_rating`/`power_rating`), `properties`, the bulk-loop pattern. Quote the resistor/cap/IC/LED `select_part` calls from `components.py` (note the LED `{"K":"1","A":"2"}` mapping). Name the source.

- [ ] **Step 6: Write references/walkthrough-555-components.md**

A narrated read of `examples/timer_555_led_blinker/components.py` end to end: definitions → nets → instances → connectivity → selected parts → `dnp`. Link to the file; quote the key blocks.

- [ ] **Step 7: Verify snippet provenance (all four references + SKILL.md)**

Run the provenance check from Task 3 Step 4 against each file in `skills/volt-component-authoring/` (change the path). Expected `MISSING: none` for each. Fix any unresolved symbol.

- [ ] **Step 8: Commit**

```bash
git add skills/volt-component-authoring
git commit -m "Rewrite volt-component-authoring with concrete API and cookbooks

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: Rewrite volt-circuit-authoring

**Files:**
- Modify: `skills/volt-circuit-authoring/SKILL.md`
- Create: `skills/volt-circuit-authoring/references/connectivity.md`
- Create: `skills/volt-circuit-authoring/references/project-framework.md`
- Create: `skills/volt-circuit-authoring/references/project-tests.md`
- Create: `skills/volt-circuit-authoring/references/walkthrough-555-main.md`

**Interfaces:**
- Consumes: `docs/python-api.md` §"Current Logical Authoring"/"Project Framework"; `examples/timer_555_led_blinker/main.py` + `project_tests.py`; `examples/stm32_usb_buck/`.
- Produces: the logical-authoring + Project skill the schematic/PCB skills build on.

- [ ] **Step 1: Confirm the circuit + project + check API surface**

Run: `grep -nE "def (net|net_class|instantiate|validate|validate_for_pcb)" python/volt/design.py | head && echo "--- project ---" && grep -nE "def (design|schematic|board|run|run_through)" python/volt/project.py | head && echo "--- checks ---" && grep -nE "def (net|connects|no_connection|places|has_outline|names|designs|schematics|boards|design|board)" python/volt/project_checks.py | head -40`
Expected: all named methods resolve. Use only what appears.

- [ ] **Step 2: Rewrite SKILL.md**

`description:` triggers on "create a Volt circuit / nets / connect pins / logical design / Project / project tests / diagnostics". Body: `volt.Design`; `design.net(name, kind=, voltage=)`; instances via catalog or `instantiate`; connectivity `net += pin` / `net += (a, b)`, pin access `r1[1]` and `d1["A"]`; typed intent + net classes; the `Project` framework + `ProjectResource` + `BuildContext`; project tests; diagnostics-vs-exceptions; deterministic artifacts. Keep/refresh the Validation Checklist. Open with "First read `../shared-volt-architecture.md`."

- [ ] **Step 3: Write references/connectivity.md**

Cheat-sheet for nets, instances, the `+=` operator forms, pin access by number/name, and the kernel-only connectivity rule. Quote the net/connectivity block from `examples/timer_555_led_blinker/components.py` (the `nets[...] += ...` section). Name the source.

- [ ] **Step 4: Write references/project-framework.md**

Cookbook for `volt.Project`, the three stage decorators, returning `(design, volt.ProjectResource("nets", nets), volt.ProjectResource("parts", parts))`, `context.design()`/`context.resource("nets", dict)`, `project.run()`, `project.run_through(project.design)`, `result.ok`/`result.write(...)`/`result.write_artifacts(...)`. Quote `build_project`/`run_project`/`main` from `examples/timer_555_led_blinker/main.py`. Name the source.

- [ ] **Step 5: Write references/project-tests.md**

Patterns for `@project.design.test` / `.schematic.test` / `.board.test`: `check.net(...).connects(...)`, `check.no_connection(...)`, `check.places(...)`, `check.has_outline()`, and the multi-model `check.names()/designs()/boards()` forms. Quote `examples/timer_555_led_blinker/project_tests.py`. Name the source.

- [ ] **Step 6: Write references/walkthrough-555-main.md**

Narrated read of `main.py`: how design/schematic/board stages and tests compose into one `Project`, and how `_require_clean` gates the write. Point to `examples/stm32_usb_buck/` as the multi-file project example. Link the files.

- [ ] **Step 7: Verify snippet provenance**

Run the provenance check against each file in `skills/volt-circuit-authoring/`. Expected `MISSING: none`. Fix unresolved symbols.

- [ ] **Step 8: Commit**

```bash
git add skills/volt-circuit-authoring
git commit -m "Rewrite volt-circuit-authoring with connectivity and Project cookbooks

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: Rewrite volt-schematic-authoring (drawing() sugar first)

**Files:**
- Modify: `skills/volt-schematic-authoring/SKILL.md`
- Create: `skills/volt-schematic-authoring/references/drawing-session.md`
- Create: `skills/volt-schematic-authoring/references/two-terminal-builder.md`
- Create: `skills/volt-schematic-authoring/references/low-level-api.md`
- Create: `skills/volt-schematic-authoring/references/walkthrough-555-schematic.md`

**Interfaces:**
- Consumes: `python/volt/schematic.py` (the `SchematicDrawing` + `SchematicTwoTerminalElement` surfaces); `docs/design/schemdraw-style-schematic-authoring.html`; `examples/timer_555_led_blinker/schematic.py`; `examples/schematic_sugar/*`.
- Produces: the schematic skill (preferred drawing API).

- [ ] **Step 1: Confirm the drawing + two_terminal API surface**

Run: `grep -nE "def [a-z]" python/volt/schematic.py | grep -v "def _" | sed -n '1,90p'`
Expected: lists the drawing-session methods (`two_terminal`, `place`, `connect`, `wire`, `power_stub`, `ground_stub`, `signal_tag`, `stack`, `hold`, `frame`, `push`, `pop`, `move`, `here`, …) and the `SchematicTwoTerminalElement` chain (`at`, `anchor`, `drop`, `between`, `to`, `tox`, `toy`, `length`, `right`, `left`, `up`, `down`, `reverse`, `flip`, `label_ref`, `label_value`, `dot`, `idot`). Document only these.

- [ ] **Step 2: Rewrite SKILL.md**

`description:` triggers on "draw a Volt schematic / make the schematic look good / place symbols / wires / labels / schematic SVG". Body: precondition (connectivity exists); sheet setup (`design.schematic(name, size=, orientation=, title=, number=, page_count=, revision=, date=, project=, file=, margins=, coordinate_zones=, grid=)`); **preferred path** = `with sheet.drawing(at=, unit=) as drawing:` with the `two_terminal` fluent builder + `connect`/`power_stub`/`ground_stub`/`local_label`/`signal_tag`/`junction`/`no_connect` + relative cursor tools (`hold`, `push/pop`, `move`, anchors `.up()/.down()/.left()/.right()/.tox()/.toy()`); **fallback path** = low-level `sch.place()/sch.wire().from_().to().orthogonal()/sch.power/ground/label`; layout-for-reading guidance; the visual quality rubric (tag restraint, spacing, no collisions, split sheets); "open the SVG." Open with "First read `../shared-volt-architecture.md`."

- [ ] **Step 3: Write references/drawing-session.md**

Toolbox for the `drawing()` session: every method from Step 1's first group, with a one-line purpose and a quoted usage from `examples/timer_555_led_blinker/schematic.py`. Name the source.

- [ ] **Step 4: Write references/two-terminal-builder.md**

Full reference for the `SchematicTwoTerminalElement` chain (the methods from Step 1's second group), each with signature intent and a real example line. Quote the `ra`/`rb`/`timing_cap`/`led` builders from `schematic.py`. Name the source.

- [ ] **Step 5: Write references/low-level-api.md**

The fallback placement API (`sch.place`, `sch.wire(net).from_().via().to().direct()/.orthogonal()`, `sch.power/ground/label/junction/sheet_port/off_page/no_connect`), drawn from `docs/python-api.md` §"Schematic Placement". State clearly it is the fallback for cases the drawing session does not cover.

- [ ] **Step 6: Write references/walkthrough-555-schematic.md**

Narrated read of `schematic.py`: sheet setup → drawing session → grouping → relative builders → labels/stubs, and why the result reads cleanly. Reference `examples/schematic_sugar/timer_555_led_blinker.py` as a second worked sugar example. Link the files.

- [ ] **Step 7: Verify snippet provenance**

Run the provenance check against each file in `skills/volt-schematic-authoring/` (extend the regex prefixes to include `sheet` and `drawing`). Expected `MISSING: none`. Fix unresolved symbols.

- [ ] **Step 8: Commit**

```bash
git add skills/volt-schematic-authoring
git commit -m "Rewrite volt-schematic-authoring leading with the drawing() API

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 7: Rewrite volt-pcb-authoring (structure + manufacturing)

**Files:**
- Modify: `skills/volt-pcb-authoring/SKILL.md`
- Create: `skills/volt-pcb-authoring/references/board-structure.md`
- Create: `skills/volt-pcb-authoring/references/manufacturing-package.md`
- Create: `skills/volt-pcb-authoring/references/walkthrough-pcb-led-board.md`

**Interfaces:**
- Consumes: `python/volt/pcb.py` + `python/volt/manufacturing.py`; `docs/python-api.md` (manufacturing package); `docs/design/pcb-json-format.html`, `docs/design/kicad-pcb-export-handoff.html`; `examples/timer_555_led_blinker/board.py` (structure), `examples/pcb_led_board/main.py`.
- Produces: the board-structure + manufacturing-handoff skill; cross-links to `volt-pcb-layout` for placement/routing.

- [ ] **Step 1: Confirm the board-structure + manufacturing API surface**

Run: `grep -nE "def (board|add_layer|set_layer_stack|set_design_rules|set_rectangular_outline|add|validate_for_pcb)" python/volt/pcb.py python/volt/design.py 2>/dev/null | head -40 && echo "--- mfg ---" && grep -nE "def (write_manufacturing_package)|class (ManufacturingPackage|ManufacturingPackageError|CapabilityProfile)" python/volt/project.py python/volt/manufacturing.py 2>/dev/null | head`
Expected: structure methods + `Hole`, `write_manufacturing_package`, `ManufacturingPackageError`, capability profile all resolve. Document only what appears.

- [ ] **Step 2: Rewrite SKILL.md**

`description:` triggers on "Volt PCB / board structure / stackup / design rules / Gerbers / BOM / CPL / manufacturing package / KiCad export". Body: board-readiness gate (`design.validate_for_pcb()`, selected parts, footprints, BOM readiness); board structure (`design.board`, `add_layer`, `set_layer_stack`, `set_design_rules`, `set_rectangular_outline`, `board.add(volt.Hole(...))`, capability profile); manufacturing handoff (`result.write_manufacturing_package(path, board=, manufacturing_profile=, archive=)` + `volt export manufacturing`, the produced fileset, `ManufacturingPackageError`); KiCad `PCB_KICAD_FAB_EXPORT_LOSS`; artifact review. **Add a cross-link**: "For placement and routing craft, use `volt-pcb-layout`." Open with "First read `../shared-volt-architecture.md`."

- [ ] **Step 3: Write references/board-structure.md**

Cookbook for layers/stackup/design-rules/outline/holes. Quote the `board = design.board(...)` → `add_layer` → `set_layer_stack` → `set_design_rules` → `set_rectangular_outline` → `board.add(volt.Hole(...))` block from `examples/timer_555_led_blinker/board.py`. Name the source.

- [ ] **Step 4: Write references/manufacturing-package.md**

Walkthrough of `result.write_manufacturing_package(...)`: arguments, the produced set (native Gerber/Excellon, BOM, CPL, diagnostics, profile metadata, `native-fabrication.json`, `manifest.json`, `inspection.html`, optional zip), the `ManufacturingPackageError` gate, and the `volt export manufacturing` CLI equivalent. Draw from `docs/python-api.md` and `python/volt/manufacturing.py`. Quote a real `write_manufacturing_package` call.

- [ ] **Step 5: Write references/walkthrough-pcb-led-board.md**

Narrated read of `examples/pcb_led_board/main.py` from readiness → structure → output. Link the file.

- [ ] **Step 6: Verify snippet provenance**

Run the provenance check against each file in `skills/volt-pcb-authoring/`. Expected `MISSING: none`. Fix unresolved symbols.

- [ ] **Step 7: Commit**

```bash
git add skills/volt-pcb-authoring
git commit -m "Rewrite volt-pcb-authoring for structure and manufacturing handoff

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 8: Align volt-pcb-layout (relative-first + fuller toolset)

**Files:**
- Modify: `skills/volt-pcb-layout/SKILL.md`
- Create: `skills/volt-pcb-layout/references/layout-grammar.md`
- Create: `skills/volt-pcb-layout/references/walkthrough-555-routing.md`

**Interfaces:**
- Consumes: `python/volt/_pcb_layout.py` (the `BoardLayout`/`BoardRoute`/`BoardTwoPadComponent`/`BoardAnchor` surfaces); `examples/timer_555_led_blinker/board.py` (routing portions).
- Produces: the aligned placement/routing skill, cross-linked from `volt-pcb-authoring`.

- [ ] **Step 1: Confirm the full relative layout API surface**

Run: `grep -nE "def [a-z]" python/volt/_pcb_layout.py | grep -v "def _" | sed -n '1,80p'`
Expected: lists `place`, `two_pad`, `route`, `connect`, `bundle`, `via`, `stitch`, `fanout`, `node`, `polygon`, `rect`, `zone`, `keepout`, `text`, `stack`, `hold`, `frame`, `align`, `distribute`, `mirror`, `move`, `move_from`, `snap`/`snap_x`/`snap_y`, `rule`; the route builder (`at`, `to`, `through`, `tox`, `toy`, `left/right/up/down`); `two_pad` chain (`at`, `anchor`, `drop`, `right/left/up/down`, `pad`, `pin`); anchor helpers (`left/right/up/down/offset/tox/toy`). Document only these.

- [ ] **Step 2: Update SKILL.md — add the relative-first principle**

At the top of the workflow, add the explicit principle (Global Constraints wording): place anchor parts (central IC, connectors, mounting refs) with explicit coords + `locked=True`, then derive everything else relatively; `snap()` anything reused; absolute tuples are the escape hatch, used only when needed. Keep the 7-category rubric and anti-patterns. Add "First read `../shared-volt-architecture.md`."

- [ ] **Step 3: Update SKILL.md — document the fuller toolset and cross-link**

Expand the placement/routing sections to mention the tools from Step 1 that the current skill omits (`align`, `distribute`, `stack`, `mirror`, `bundle`, `fanout`, `stitch`, `keepout`, `zone`, `frame`, `route(...).through(...)`), each one line. Add a cross-link: "For board structure and manufacturing output, use `volt-pcb-authoring`."

- [ ] **Step 4: Write references/layout-grammar.md**

Full reference for `board.layout(unit=, grid=)`: each method from Step 1 with a one-line purpose and a real usage line. Quote `two_pad`/`hold`/`snap`/`route(...).at().tox().toy()`/`via` from `examples/timer_555_led_blinker/board.py`. Name the source.

- [ ] **Step 5: Write references/walkthrough-555-routing.md**

Narrated read of the routing portion of `board.py`: the GND backbone (drop + via + back-layer stitch), the +5V rail, and the per-net escapes — showing relative anchors and `snap` in action. Link the file.

- [ ] **Step 6: Verify snippet provenance**

Run the provenance check against each file in `skills/volt-pcb-layout/` (extend prefixes to include `layout`, `route`). Expected `MISSING: none`. Fix unresolved symbols.

- [ ] **Step 7: Commit**

```bash
git add skills/volt-pcb-layout
git commit -m "Align volt-pcb-layout: relative-first principle and full grammar

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 9: Final integration pass

**Files:**
- Modify: any skill file needing a cross-link or description fix (as found).

**Interfaces:**
- Consumes: all six skills from Tasks 2–8.
- Produces: a coherent, verified skill set.

- [ ] **Step 1: Cross-links resolve**

Run: `grep -rnoE "\.\./[a-z-]+|volt-[a-z-]+" skills/*/SKILL.md | grep -oE "volt-[a-z-]+" | sort -u`
Manually confirm every referenced skill name is one of the six real folders. Fix any typo or dangling reference.

- [ ] **Step 2: Every skill has the required shape**

Run: `for d in skills/volt-*; do echo "== $d =="; test -f "$d/SKILL.md" && head -4 "$d/SKILL.md" | grep -E "name:|description:"; test -d "$d/references" && ls "$d/references" || echo "(no references/)"; done`
Expected: every skill has `name:`/`description:` frontmatter; every skill except possibly `volt-basics` has a populated `references/`. Fix any missing frontmatter.

- [ ] **Step 3: Whole-tree provenance sweep**

Run the provenance check (Task 3 Step 4) once over every `skills/**/*.md` with the union of prefixes (`volt|design|d|project|result|sheet|drawing|board|layout|component|part|net`). Expected `MISSING: none` across the set. Fix any straggler.

- [ ] **Step 4: Fresh-agent skim test**

Read `volt-basics` → `volt-component-authoring` → `volt-circuit-authoring` → `volt-schematic-authoring` → `volt-pcb-authoring` → `volt-pcb-layout` in order. Confirm a reader can follow a complete path from defining a component to writing a manufacturing package without needing the docs first. Note and fix any gap (missing step, undefined term, broken handoff).

- [ ] **Step 5: Commit**

```bash
git add skills
git commit -m "Final integration pass on official Volt skills

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review (completed by plan author)

**Spec coverage:** structure/migration → Task 1; shared md refresh → Task 2; volt-basics → Task 3; component/circuit/schematic/pcb-authoring rewrites → Tasks 4–7; pcb-layout alignment → Task 8; cross-links + trigger descriptions + provenance → Task 9. All spec sections map to a task.

**Placeholders:** content lists and exact API surfaces are given per task; the "what to write" is concrete (named methods, named source files), not "fill in details." Snippets are intentionally sourced from named example files rather than re-pasted in full here, because the executing agent quotes the real, current file (the anti-drift discipline) rather than a copy that could go stale in the plan.

**Type/name consistency:** skill folder names are used identically across tasks (`volt-basics`, `volt-component-authoring`, `volt-circuit-authoring`, `volt-schematic-authoring`, `volt-pcb-authoring`, `volt-pcb-layout`); `volt-pcb-authoring` and `volt-pcb-layout` are consistently distinct. The provenance check is the same procedure reused with different path/prefix arguments.
