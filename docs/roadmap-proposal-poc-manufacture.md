# Roadmap proposal: first manufactured proof-of-concept board

**Status:** approved 2026-06-09; Linear tickets created (see Outcome) · **Date:** 2026-06-09 ·
**Author:** drafted with Claude from routing research, Phil's Lab transcript analysis
(`docs/transcripts/`), and a full review of the VoltDev Linear workspace (196 issues, as of
2026-06-09).

## Goal

Get a first real board — the **Volt Pulse Badge** (45×45 mm, 2-layer, CR2032 + TLC555 astable +
NPN driver + 5 LEDs, authored in the sibling `Boards` repo) — **fabricated and assembled**, with
the entire flow (design → schematic → layout → routing → verification → manufacturing handoff)
done **in code through Volt**, no interactive-EDA escape hatch.

This is deliberately foundations-first: each milestone builds a kernel-owned system that will be
extended later, not a one-off path to a single board. The STM32 USB buck benchmark (VOL-7)
remains the internal stress test; the badge is the manufacture target because it is electrically
complete, validated, hand-solderable, and cheap to get wrong.

## Current state

Done and trustworthy: logical kernel + typed electrical semantics, schematic
authoring/projection/rendering (VOL-58 children; the epic itself remains open), Project
Framework (VOL-169 children; epic open), first PCB projection with placement/copper
primitives/zones/ratsnest/first DRC slice (VOL-60, closed), first fabrication handoff artifacts
(VOL-162, closed), DRC/ERC taxonomy (VOL-182, closed).

The badge itself: circuit and polished schematic validate clean (0 errors). The PCB is placed and
**partially routed** — the analog timing cluster could not be hand-routed DRC-clean in code.

**Why it's blocked:** Volt's PCB layer records copper but doesn't solve. Routing primitives are
obstacle-blind (by design — legality is a diagnostic, not a structural invariant), copper DRC
diagnostics don't yet populate the existing overlay-geometry contract (`DiagnosticOverlay` in
`volt/core/diagnostics.hpp`; visual validation already uses it), zones don't void around
other-net pads, there is no fanout/escape help for 0805s against a SOIC-8's 1.27 mm pitch, and
the rule model is thin: one global `copper_clearance` scalar plus a per-net rule-class clearance
override already consumed by DRC — no width/via/layer rules, no class-pair resolution, no
clearance matrix.

## Gap analysis vs Linear

The four open epics (VOL-175 visual validation, VOL-176 ERC/DRC, VOL-177 manufacturability,
VOL-61 simulation) cover verification and handoff well. Three things are tracked nowhere:

1. **A rules engine.** VOL-184 ("copper clearance and width rules") and VOL-186 ("stackup,
   drills, mask") each implicitly need a shared model that mostly doesn't exist: net classes with
   priority resolution, a clearance *matrix* (track/pad/via/hole/edge/silk are different
   manufacturing processes with different tolerances), area-scoped overrides ("rooms"), and a
   first-class stackup (copper weights, dielectric thickness/εr). The kernel already owns the
   seed — `RuleClass` carries per-net `copper_clearance_mm` and `maximum_net_voltage` and DRC
   already consumes it — so this is an **extension of that one system, not a new one**. Approved
   decision: rule classes are renamed to **net classes** first, then extended. Industry tools
   treat rules-before-layout as the non-negotiable first step.
2. **A routing capability.** No ticket covers the spatial index, obstacle-aware connection,
   escape/fanout, or geometry-carrying DRC feedback. This is the gap that physically blocks the
   board.
3. **Physics-derived sizing.** PCB designers feed manufacturer tables and IPC calculators
   (width-from-current, spacing-from-dielectric-height, voltage clearance) into their tools by
   hand. Volt's design layer already knows net intent (`kind=`, `voltage=`) — these calculators
   should be **baked in**, deriving rules from declared intent. No GUI tool can do this because
   none owns the electrical intent; it is Volt's natural differentiator.

## Proposed milestones

### M1 — Rules & stackup foundation (new epic: VOL-197)

Prerequisite for VOL-184 and VOL-186; consumes VOL-44 (power intent, in progress).

- Rename rule classes to net classes across kernel, IO, Python, and docs (VOL-199) — one rule
  system, one name, before extension.
- First-class stackup: per-layer copper weight, dielectric thickness/εr, total thickness.
- Net classes extended (width, clearance, via size, allowed layers) with priority resolution;
  class×class clearance resolution rule (max-of-pair, like KiCad). Defaults **derived from net
  intent**: `kind="power"` nets auto-assign the power class.
- Clearance matrix replacing the single scalar.
- Rooms: area-scoped rule overrides (the mechanism fine-pitch escape routing needs).
- Manufacturer capability profiles (first instance: JLCPCB 2-layer) as importable presets, with
  "distance from manufacturing minimum" as a lint, not just a floor.
- **Baked-in calculators** (the transcript point): IPC-2221/2152 width-from-current+temp-rise,
  IPC spacing-from-dielectric-height (1H stripline / 2H microstrip), voltage-derived clearance.
  API shape: `net_class(current=1.0, temp_rise=10)` → computed width. Closed-form, well
  documented, small.

### M2 — Routing engine v1 (new epic: VOL-198)

Layer ownership, fixed up front: the spatial index, legality predicates, and routing algorithms
live in `Volt::PCB` (C++); Python provides call syntax only. Obstacle-aware connection is an
authoring-time solver — `add_track` keeps accepting illegal copper, and legality stays a DRC
diagnostic. Borrows directly from industry-standard technique research (FreeRouting, KiCad P&S,
pattern routing — see references):

- Clearance-class-inflated spatial index shared by routing **and** DRC (Minkowski inflation:
  legality check = overlap test). Build it class-aware from day one; retrofitting per-net widths
  forces an index redesign.
- DRC diagnostics carry geometry (position, actual vs required) — ride VOL-180's overlay
  contract rather than inventing a second one.
- `connect(a, b, avoid=True)`: pattern routing first (straight/L/Z), walk-around fallback.
- `escape(component)`: fanout stubs for fine-pitch parts, creating an explicit serialized room
  entity (never implicit state).
- **Deferred from v1:** full `autoroute()` (A* + rip-up/reroute), Manhattan per-layer direction
  mode, topological routing, auto-voiding zones. v1 only needs assisted code-routing to
  converge; the index and rules make the later autorouter a pathfinding problem, not a rewrite.

### M3 — Verification gates (existing tickets, now consuming M1/M2)

- VOL-184 PCB DRC expansion — including routed-connectivity-vs-logical-nets, the "board is
  actually finished" gate.
- VOL-183 logical ERC, VOL-185 regression fixtures.
- VOL-180 visual diagnostics in SVG/JSON, VOL-181 benchmark gating.

### M4 — Manufacturing handoff (existing VOL-177, as scoped; parallelizable with M2/M3)

- VOL-186 manufacturability profiles (consumes M1's stackup + profile model).
- VOL-187 golden fixtures + external referee (KiCad gerber viewer/DRC) before trusting exports.
- VOL-188 BOM + component placement (CPL) artifacts.
- VOL-189 gate the real-board benchmark on a deterministic manufacturing bundle.

### M5 — Fabricate the Pulse Badge (Boards repo)

- Finish badge routing in code using M2 primitives.
- Pass the full chain: ERC → DRC → visual validation → manufacturability → bundle.
- Upload to JLCPCB, manual review in their gerber viewer, order.
- Bring-up checklist: battery polarity, measured blink rate vs predicted ~2.1 Hz
  (`f = 1.44/((RA+2RB)·CT)`), LED current, current draw.

### Simulation track (VOL-61) — amended, runs in parallel

VOL-61's architecture is right (kernel-owned backend-neutral contracts; SPICE as adapter, never
canonical; ERC first). Three amendments:

1. **Name tier 1: analytical checks, now.** Closed-form predictions as project tests — blink
   frequency, LED current at 3.0 V nominal *and* 2.4 V battery end-of-life (Vf ≈ 1.8 V; the
   margin is thin and this is the single most valuable pre-order check), Q1 forced beta, CR2032
   life estimate. Zero kernel work; these are tests computing predictions, not Python owning
   model semantics, so they don't violate the epic's boundary rule. Start in the Boards repo
   immediately; later promote the pattern into a Volt affordance (`check.value()`-style).
2. **First backend goal = DC operating point, not transient** (amend VOL-74): netlist export +
   ngspice subprocess adapter. Most practical confidence (rail voltages, currents, saturation,
   power) is DC.
3. **Part framework carries `spice_model` references** when tier 2 starts, the same way parts
   carry footprints.

Tier 1 + M5 closes the signature loop for the whole project: *prediction in code → fabricated
board → physical measurement*. Later tiers unlock solve-for-value part selection, Monte Carlo
over tolerances, and behavioral regression tests in CI — and give agent workflows behavioral
feedback ("works"), complementing DRC's geometric feedback ("legal").

## Critical path and sequencing rationale

```
VOL-44 → M1 rules → M2 routing v1 → finish badge routing → M3/M4 gates → order (M5)
                          ↘ M4 (VOL-186/187/188) can start once M1's profile model exists
Simulation tier 1: immediate, parallel, no dependencies.
```

M1 before M2 because the router consumes the rules (the index design depends on the clearance
matrix). M2 before M3 because there is no point gating a board that cannot be finished. M4 is
mostly independent of routing. Explicitly **not started**: VOL-61 tiers 2–3 beyond contracts
(VOL-71 is paper-only and can happen anytime), VOL-59 KiCad import, full autoroute,
impedance/differential-pair/delay-matching rules (the M1 rules model must merely leave room for
impedance profiles later).

## Outcome (approved 2026-06-09)

Both epics were approved with amendments (extend-don't-duplicate the kernel rule system, rename
rule classes to net classes, kernel ownership of routing pinned, explicit rooms), and Linear was
updated on 2026-06-09:

- **VOL-197 — Epic: Net class rules and stackup foundation** (blocked by VOL-44; blocks VOL-184,
  VOL-186). Children: VOL-199 rename rule classes to net classes; VOL-200 stackup model; VOL-201
  net-class extension + priority/class-pair resolution; VOL-202 clearance matrix; VOL-203
  explicit room entities; VOL-204 manufacturer capability profiles (blocks VOL-186); VOL-205 IPC
  calculators with provenance.
- **VOL-198 — Epic: Assisted PCB routing engine v1** (blocked by VOL-197). Children: VOL-206
  shared spatial index; VOL-207 copper DRC overlay geometry (related: VOL-180); VOL-208
  obstacle-aware connect; VOL-209 escape/fanout.
- **VOL-210 — Fabricate and assemble the Volt Pulse Badge** (blocked by VOL-198, VOL-184,
  VOL-186, VOL-187, VOL-188; related: VOL-181, VOL-189).
- **Simulation amendments**: VOL-211 tier-1 analytical prediction checks (under VOL-61); VOL-212
  `spice_model` part references (under VOL-61); VOL-74 description amended to DC operating point
  first; approval comment recorded on VOL-61.

## References

- Transcripts: `docs/transcripts/pcb_traces_101`, `docs/transcripts/pcb_design_rules_101`
  (Phil's Lab) — source of the rules-before-layout ordering, the clearance-matrix and net-class
  model, rooms, and the calculator workflow this proposal bakes in.
- Routing technique research: FreeRouting architecture (clearance inflation, expansion rooms,
  rip-up cost escalation, pull-tight) — tinycomputers.io "The Mathematics of PCB Trace Routing";
  KiCad push-and-shove/walk-around — KiCad interactive router docs; pattern routing, Lee/A*,
  Mikami-Tabuchi/Hightower — NTU EDA routing course notes; topological routing — TopoR, gEDA
  Toporouter (Dayan); code-first precedent — tscircuit autorouting benchmark dataset
  (github.com/tscircuit/autorouting).
- Linear: epics VOL-175/176/177/61; key open tickets VOL-44, VOL-180/181, VOL-183/184/185,
  VOL-186/187/188/189.
- Badge state: `Boards/boards/volt_pulse_badge/` (design + schematic validate clean; board
  partially routed, timing cluster left as ratsnest).
