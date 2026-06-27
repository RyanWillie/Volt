---
name: volt-pcb-layout
description: Use when authoring or improving a Volt PCB board layout — placing footprints and routing copper from a logical circuit so the board is electrically correct AND looks professional, clean, and well structured. Drives the structured Board.layout(...) grammar (anchors, relative placement, octilinear routing) to an advisory layout-quality rubric. Triggers on requests to "lay out / place / route a board or PCB", "make the board look professional", or "clean up a board layout".
---

# Volt PCB Layout

> First read `../shared-volt-architecture.md`.
> For board structure and manufacturing output (layers, outline, design rules, mounting holes, KiCad export), use `volt-pcb-authoring`.

Author a professional, clean PCB layout from a Volt logical circuit using the structured
board layout grammar. The board must be **electrically correct** (DRC-clean, every net
routed) and **look professional** (grid-aligned, sensibly placed, cleanly routed, readable
silkscreen).

This skill is **guidance, not a gate**. The rubric below is advisory: it tells you what
"done well" looks like. Nothing here blocks authoring. Physical correctness is enforced
separately by DRC (`board.validate()`); this skill is about the quality layered on top.

You place and route **by hand** through the structured grammar. This is not auto-routing —
there is no solver. The grammar removes hand-calculated coordinates; your judgment supplies
the layout.

## Preconditions

- A logical circuit (`Design`) exists with nets and components.
- Each placed component has a **selected physical part** with footprint geometry and a
  pin→pad map (`part.select_part(... footprint=..., pin_pads=...)`). Verify with
  `design.validate_for_pcb()` before placing — unresolved footprints make pad anchors fail.
- You know the board's intent: rough size, layer count, which net is ground, which parts
  are connectors / mounting references.

## The rubric (what "professional" means)

Seven categories. Keep them in view through every phase. Severities are advisory.

1. **Grid & orientation** — everything on grid; rotations are multiples of 90°; like parts
   share an axis.
2. **Spacing & clearance** — courtyards never overlap and aren't cramped; parts clear the
   board edge; even spacing within a group; mounting-hole keepouts respected.
3. **Placement logic** — decoupling caps hug their IC power pin (≤ 2 mm, same side);
   connectors and holes at edges; functional blocks cluster; ratsnest kept short.
4. **Routing geometry** — segments only 0°/45°/90°, no acute corners; clean pad entry; few
   jogs.
5. **Routing conventions** — track width matches net class (power/ground wider than
   signal); vias only on layer changes; optional per-layer direction; ground pour present.
6. **Silkscreen & docs** — every part has a non-overlapping, right-way-up designator; silk
   clear of pads/holes; polarity/pin-1 marks; board title and revision.
7. **Composition** — component centroid near board center; sensible board utilization;
   consistent orientation of like parts.

## The relative-first principle

The layout grammar is designed so that almost nothing requires a hand-calculated coordinate.
Follow this discipline:

1. **Place a small set of anchors explicitly.** Give the central IC, connectors, and
   mounting reference parts explicit coordinates and `locked=True`. These are the fixed
   skeleton the rest of the board hangs from.
2. **Derive everything else relatively.** Use anchor offsets (`.up()`, `.down()`,
   `.left()`, `.right()`, `.offset()`), axis projections (`.tox()`, `.toy()`), and layout
   helpers (`layout.stack()`, `layout.align()`, `layout.distribute()`, `layout.mirror()`)
   to position all other parts relative to the anchors already placed. Never invent a
   hand-measured mm value when an anchor-derived expression gives the same point.
3. **Snap anything you reuse.** `layout.snap(anchor)` or `layout.node(anchor)` rounds a
   derived anchor to the layout grid and gives it a name. Store snapped points that appear
   in more than one route or placement.
4. **Absolute `(x, y)` tuples are the escape hatch**, used only when a coordinate truly has
   no meaningful anchor-relative expression (e.g. the board center, or a fixed mechanical
   constraint). When you use one, snap it immediately.

```python
# Skeleton — locked, explicit
header = layout.place(parts["J1"],
                      at=board.edge("left").center().right(7),
                      orient="right", locked=True)
timer  = layout.place(parts["U1"], at=(23.0, 15.0), orient="right", locked=True)

# Everything else is relative
cdec = layout.two_pad(parts["CDEC"]).at((29.0, 10.0)).anchor("center").right()
power_rail = layout.snap(header[1].up(5.15))   # reused — snap it
timing_junction = layout.snap(rb.end.right(2.0))
```

## Workflow

Work the phases in order. Each names the rubric criteria it serves.

### 0. Read the circuit before touching geometry

Build a mental (or written) map first:

- **Nets & classes** — which nets are power/ground (`net.kind`), their rule class and
  intended track width.
- **Functional blocks** — group components by sub-circuit (e.g. timing RC, decoupling,
  LED + series resistor). Blocks place together.
- **Decoupling caps** — a two-pad capacitor across a power net and ground, sitting near an
  IC, is a decoupling cap. Note which IC power pin each serves (power pins are
  `ElectricalTerminalKind::Power`).
- **Edge parts** — connectors and mounting holes belong at the board boundary.

Do not start placing until you can name each part's role and block.

### 1. Board setup — *rubric 1, 2*

```python
board = design.board("My Board")
front = board.add_layer("F.Cu", role="copper", side="top")
back  = board.add_layer("B.Cu", role="copper", side="bottom")
silk  = board.add_layer("F.SilkS", role="silkscreen", side="top")
board.set_layer_stack((front, back), thickness=1.6)
board.set_design_rules(copper_clearance=0.20, min_track_width=0.20,
                       board_outline_clearance=0.25)
board.set_rectangular_outline(origin=(0.0, 0.0), size=(W, H))
# mounting holes at corners
board.add(volt.Hole(center=(4.0, 4.0), diameter=2.2, role="mounting"))
```

Pick the board size *after* a rough placement estimate so utilization lands in band — start
generous, tighten once placed. Put mounting holes at the corners first; they constrain the
edge keepout.

Board structure belongs to `volt-pcb-authoring`; layout opens only after the board handle
and layers exist.

### 2. Placement — *rubric 1, 2, 3, 7*

Open the layout with an **explicit grid** so placement and routing snap. Use `unit` for the
default stack pitch.

```python
with board.layout(unit=1.0, grid=0.5) as layout:
    # connectors and edge parts first, anchored to the board edge
    header = layout.place(parts["J1"],
                          at=board.edge("left").center().right(7),
                          orient="right", locked=True)
    # the central IC, locked as the anchor everything else references
    u1 = layout.place(parts["U1"], at=board.center, orient="right", locked=True)
```

Then place each functional block, anchoring to the parts it connects to rather than to bare
coordinates:

- **Connectors / mounting refs to the edges** (rubric 3). Anchor with `board.edge(...)`
  and `board.corner(...)`.
- **Decoupling caps hug the power pin** (rubric 3). Place the cap a grid step or two off the
  IC's power pin anchor, on the same side:
  ```python
  cdec = (layout.two_pad(parts["CDEC"])
              .at(u1.VCC.up(2.0)).anchor("center").right())
  ```
  Keep it within ~2 mm of `u1.VCC`.
- **Cluster a block** with `layout.hold()` so the cursor returns after the block, and align
  members on a shared axis (rubric 1, 2):
  ```python
  with layout.hold():
      ra = layout.two_pad(parts["RA"]).at((36.0, 10.0)).anchor("center").right()
      rb = layout.two_pad(parts["RB"]).at(layout.snap(ra.center.down(2.0))).anchor("center").right()
  ```
- **Evenly-spaced banks** — `layout.stack(count=, pitch=)` generates the anchor series; use
  `layout.distribute(count=, start=, end=)` when the endpoints are already fixed.
- **Mirror a group** — `layout.mirror(anchors, axis="x")` reflects the x coordinate
  (across a vertical line, mirroring left↔right); `axis="y"` reflects the y coordinate
  (across a horizontal line, mirroring top↔bottom).
- **Consistent orientation** (rubric 7) — orient like parts the same way unless routing
  truly demands otherwise.
- Snap any derived anchor you reuse: `layout.snap(anchor)` or `layout.node(anchor)`.

**Fuller placement toolset** — methods the current skill introduced:

| Method | Purpose |
|---|---|
| `layout.align(anchors, axis=, target=)` | Align a set of anchors along one board axis |
| `layout.distribute(count=, start=, end=)` | Even spacing between two fixed endpoints |
| `layout.stack(count=, direction=, pitch=, at=)` | Evenly-spaced anchor series from cursor |
| `layout.mirror(anchors, axis=, about=)` | Reflect anchors across x or y axis |
| `layout.move(dx=, dy=)` | Shift cursor by relative offset |
| `layout.move_from(anchor, dx=, dy=, direction=)` | Jump cursor to anchor ± offset |
| `layout.bundle(pairs, layer=, net=, width=, mode=)` | Route multiple independent pairs as a batch |
| `layout.fanout(anchors, layer=, direction=, distance=, via_layers=)` | Escape pads outward, optionally add vias |
| `layout.stitch(net, at=, start_layer=, end_layer=)` | Add a deterministic set of vias for one net |
| `layout.keepout(layers=, restrictions=, outline=, at=, size=)` | Constrain copper/vias/text within an area |
| `layout.zone(layers=, net=, outline=, at=, size=, fill=)` | Add a copper fill zone |
| `layout.rect(at=, size=)` / `layout.polygon(vertices)` | Compute rectangular or arbitrary outlines |
| `layout.frame(at=, direction=)` | Temporarily switch to a local coordinate origin |
| `layout.route(...).through(point)` | Route via an intermediate waypoint before `.to()` |

**Before routing, sanity-check placement:** render `board.to_svg()` and **view it as an
image** (rasterize first if your tooling needs it — see "Viewing Rendered Output" in
`../shared-volt-architecture.md`). Is the ratsnest short and untangled? Do courtyards
breathe? Are connectors on the edge and the centroid roughly centered? Re-place now — it
is far cheaper than re-routing.

### 3. Routing — *rubric 4, 5*

Route to **pad anchors**, never to invented coordinates. Let octilinear mode draw clean
corners.

```python
# power/ground wider than signal (rubric 5)
layout.route(nets["+5V"], layer=front, width=0.30).at(header[1]).to(u1.VCC)
layout.route(nets["OUT"], layer=front, width=0.20).at(u1.OUT).to(rled.start)
```

- **Width by net class** (rubric 5): power and ground wide, signal narrow; keep a class
  consistent.
- **Octilinear by default** (rubric 4): a single `.to(pad)` auto-inserts a clean corner.
  Reach for `.tox()/.toy()` to shape a route; chain `.through(waypoint)` for multi-bend
  runs; use `mode="direct"` *only* for an intentional single 45° shot, never to "just
  connect two points".
- **Vias only on a real layer change** (rubric 5): `layout.via(net, at=anchor,
  start_layer=front, end_layer=back)`. Don't via to dodge a route you could have placed
  around.
- **Connect shorthand** — `layout.connect(start, end, layer=, net=, width=, through=(),
  mode=)` routes between two anchors in one call; `through=(waypoint,)` threads it through
  an intermediate node.
- **Bundle parallel nets** — `layout.bundle(pairs, layer=, width=, mode=)` routes a list
  of `(start, end)` anchor pairs as a batch.
- **GND backbone pattern** — route short drops from each ground pad, via to the back layer,
  then stitch them together with `layout.route(gnd, layer=back)` segments. Reserve
  `layout.stitch(net, at=..., start_layer=, end_layer=)` for adding multiple stitching
  vias deterministically.
- **Fanout from dense ICs** — `layout.fanout(anchors, layer=, direction=, distance=,
  via_layers=)` escapes a cluster of pads outward before detailed routing.
- **Per-layer direction** (rubric 5, advisory): if the board benefits, keep front mostly
  horizontal and back mostly vertical.
- **Ground pour** (rubric 5): `layout.zone(layers=(back,), net=nets["GND"], at=...,
  size=...)` rather than routing every ground return by hand.
- **Keepout areas**: `layout.keepout(layers=..., restrictions=..., at=..., size=...)` for
  mounting-hole keep-clear zones, RF quiet areas, or mechanical clearances.

### 4. Finish — silkscreen & docs — *rubric 6*

```python
board.add_text("U1", at=u1.center.up(3.0), layer=silk, size=0.8)   # designator
board.add_text("My Board  Rev A", at=(W/2, H-1.5), layer=silk, size=1.0)
```

- A designator for **every** part, clear of pads and holes, rotation 0° or 90° (never
  upside-down).
- Polarity / pin-1 marks on polarized parts (LED, electrolytic, diode, connector pin 1).
- Board title and revision somewhere on silk.

### 5. Self-review — measure, then iterate

This is where "looks professional" gets verified, not assumed.

1. **Render and look** — `board.write_svg(...)` (or `board.to_svg()`), then **view the SVG
   as an image** and inspect against all seven rubric categories. Trust your eyes for the
   gestalt (rubric 7). If your tooling can't render SVG directly, rasterize it first — see
   "Viewing Rendered Output" in `../shared-volt-architecture.md`.
2. **Run DRC** — `board.validate()` must be clean (this is correctness, separate from the
   rubric). Among the checks it can raise are `PCB_COMPONENT_ASSEMBLY_CLEARANCE_WARNING`
   (package bodies closer than `package_assembly_clearance`) and
   `PCB_COMPONENT_BOARD_EDGE_CLEARANCE_VIOLATION` (a package body too near the outline) —
   resolve these by spacing parts or pulling them off the edge, the same moves rubric
   category 2 already asks for.
3. **Run the readability check** — once `validate_board_readability(board)` exists, run it
   and resolve every `warning`; weigh each `info`. Until then, walk the rubric checklist
   below by hand.
4. **Iterate** — fix the cheapest-to-move thing first (usually placement), then re-route the
   affected nets.

## Rubric checklist (manual self-review until the validator lands)

- [ ] Every placement, pad, track vertex, and via on grid; rotations multiples of 90°.
- [ ] Like parts aligned on a shared axis, consistent orientation.
- [ ] No courtyard overlaps; parts clear the board edge; even intra-group spacing.
- [ ] Decoupling caps within ~2 mm of their IC power pin; connectors/holes at edges.
- [ ] Functional blocks clustered; ratsnest short and untangled.
- [ ] Tracks 0°/45°/90° only; no acute corners; clean pad entry; few jogs.
- [ ] Width matches net class; power/ground wider; vias only on layer changes.
- [ ] Ground pour present where expected.
- [ ] Designator on every part, non-overlapping, right way up; polarity/pin-1 marks;
      title + revision present.
- [ ] Centroid near board center; utilization sensible; nothing lopsided.
- [ ] `board.validate()` (DRC) clean.

## Anti-patterns

- **Magic offsets.** `part.pin.left(2.125)` with an invented number. Derive distances from
  footprint geometry, the grid, or another anchor; snap what you reuse.
- **`direct` routes as a default.** They bypass octilinear cleanup and create acute angles.
  Reserve `direct` for a deliberate single 45°.
- **Off-grid `place(at=(x, y))`.** Explicit tuple placements skip cursor snapping. Snap them
  (`layout.snap((x, y))`) or place relative to an anchor.
- **Routing before placing well.** A tangled ratsnest is a placement problem; fix placement,
  not the routes.
- **Silk over pads/holes, or upside-down text.** Offset designators clear of copper; keep
  rotation 0° or 90°.
- **Treating the rubric as a gate.** It is advisory. Diagnose and improve; do not refuse to
  finish a board over an `info`.

## Further reading

- `references/layout-grammar.md` — full method-by-method reference for the `board.layout()` grammar.
- `references/walkthrough-555-routing.md` — narrated read of the routing section of `examples/timer_555_led_blinker/board.py`.
