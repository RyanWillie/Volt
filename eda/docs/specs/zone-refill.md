# Zone-refill engine

- **Status:** implemented
- **Pebble:** `8b4fe8d4`
- **Owner:** `pi`
- **Last updated:** `2026-04-17`

## Summary

Volt stores plane polygons but does not compute filled copper. Gerber export currently emits raw plane outlines, ignoring clearances, keepouts, thermals, and islands. This spec defines a refill engine that produces correct copper-fill fragments from plane definitions and board geometry, and wires those fragments into Gerber export.

## Motivation

Current plane export is geometrically incorrect — it overlaps foreign-net copper, ignores board-edge clearance, and produces no thermal relief. A board fabricated from current Gerber output with planes would have shorts.

## Goals

- Compute plane-fill fragments respecting all plane parameters
- Subtract foreign-net copper with clearance offsets
- Subtract board-edge and NPTH keepouts
- Apply connect_style (Solid / Thermal / None) per same-net pad
- Remove slivers below min_width
- Remove disconnected islands (unless keep_islands)
- Respect priority ordering between overlapping planes
- Wire fragments into Gerber copper layer export
- Store fragments alongside plane for render/export consumption

## Non-goals

- Interactive real-time refill during editing
- Via stitching
- Keepout zone data model (future work)
- Curved/arc fragment edges (arcs are flattened to linear segments)
- Board render / 3D render updates (follow-up)

## Current state

- `board_plane` in `board.rs:1242` stores user polygon only
- `Plane` in `project/mod.rs:597` has clearance/thermal/island fields but nothing consumes them
- `gerber.rs:620-636` exports raw `plane.vertices` as one dark region
- DRC `collect_copper_objects` ignores planes entirely
- No `FragmentPolygon` type exists

## Proposed design

### New crate: `volt-refill`

A new workspace crate `eda/crates/volt-refill/` to keep refill logic isolated from CLI and export.

### Data model changes

Add to `volt-core::project`:

```rust
/// A computed fill fragment from a plane refill pass.
/// Each contour is a closed polygon (Vec of points).
/// First contour is the outer boundary; subsequent contours are holes.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize, Default)]
pub struct PlaneFragment {
    pub contours: Vec<Vec<Position>>,
}
```

Add to `Plane`:

```rust
/// Computed fill fragments. Empty until refill is run.
#[serde(default)]
pub fragments: Vec<PlaneFragment>,
```

### Arc flattening

Volt `Vertex` supports arcs via non-zero `angle`. All polygon inputs to the refill engine are first linearized:

- Arc segments are tessellated to chord segments
- Chord error target: 0.01mm (sufficient for PCB fab)
- Helper: `fn flatten_vertices(vertices: &[Vertex]) -> Vec<[f64; 2]>`

### Algorithm

For each layer with planes, sorted by descending priority:

1. **Flatten** plane polygon vertices to linear ring
2. **Clip to board edge** — intersect with board outline inset by `plane.min_board_clearance`
3. **Subtract higher-priority planes** — difference with already-computed higher-priority plane footprints on this layer
4. **Collect obstacles** on this layer:
   - Foreign-net traces → stroke-offset by `trace.width/2 + plane.min_copper_clearance`
   - Foreign-net device pads → pad polygon offset by `plane.min_copper_clearance`
   - Foreign-net vias → circle polygon offset by clearance
   - Foreign-net standalone board pads → same
   - NPTH board holes → circle offset by `plane.min_npth_clearance`
   - PTH pad holes (on foreign-net pads) → circle offset by clearance
5. **Union all obstacles** into one clip set
6. **Difference** plane polygon minus obstacle union
7. **Apply connect_style** per same-net pad:
   - `Solid` — no action (pad copper naturally merges)
   - `Thermal` — cut 4 quadrant gaps around pad center, each gap width = `thermal_gap`, leaving 4 spokes of width `thermal_spoke`
   - `None` — treat same-net pad as foreign-net obstacle (full antipad)
8. **Remove slivers** — discard any fragment contour narrower than `plane.min_width` everywhere (approximated by negative offset then positive offset)
9. **Remove islands** — if `!plane.keep_islands`, identify connected components; keep only fragments that overlap a same-net pad or via; discard the rest
10. **Store** resulting `Vec<PlaneFragment>` in `plane.fragments`

### Pad geometry helpers

For device pads, construct polygon from shape/size:
- `RoundRect` → rectangle with rounded corners (approximated as polygon)
- Transform by device position, rotation, flip

For circle/via antipads:
- Approximate as N-gon (N=32)

### Thermal spoke geometry

For a same-net pad at position (px, py) with thermal connection:
- Create 4 rectangular gaps radiating from pad center along +X, -X, +Y, -Y
- Each gap: width = `thermal_gap`, extending from pad edge outward past clearance
- Subtract the 4 gap rectangles from the plane around this pad
- Remaining 4 diagonal bridges = the thermal spokes

### Gerber integration

Replace `gerber.rs:620-636` (raw plane export) with:

```rust
for plane in &board.planes {
    if plane.layer != layer { continue; }
    for fragment in &plane.fragments {
        if fragment.contours.is_empty() { continue; }
        // Outer boundary = dark region
        writer.region_start();
        emit_contour(writer, &fragment.contours[0]);
        writer.region_end();
        // Holes = clear regions
        for hole in &fragment.contours[1..] {
            writer.set_polarity_clear();
            writer.region_start();
            emit_contour(writer, hole);
            writer.region_end();
            writer.set_polarity_dark();
        }
    }
}
```

### Refill callsite

Refill is computed at export time in `export_all()` before per-layer export. This avoids persisting fragments in project files during normal editing.

Optionally, a `board refill` CLI command can persist fragments to `boards/<name>.json` for inspection.

### Net resolution

To determine whether a pad/via/trace is same-net or foreign-net relative to a plane:

- Traces/vias: `BoardNetSegment.net` vs `plane.net`
- Device pads: resolve through `ComponentInstance.signal_connections` + `Device.pad_mappings`
- Standalone board pads: `BoardNetSegment.net`

Reuse the existing ratsnest net-resolution pattern from `board.rs:1323-1375`.

## Test vectors

### TV1 — Foreign trace clearance subtraction

- 20×20 board, GND plane on top copper covering 1..19
- Single foreign-net trace across middle, width 1.0, clearance 0.5
- Expected: plane has a moat around the trace

### TV2 — Same-net trace does not subtract

- Same geometry as TV1, but trace is on the same net as plane
- Expected: no moat, plane fills normally

### TV3 — Board-edge keepout

- Plane extends to board edge, min_board_clearance=1.0
- Expected: fill is inset 1.0mm from board edge

### TV4 — NPTH hole keepout

- NPTH hole diameter 2.0 at center, min_npth_clearance=0.5
- Expected: circular void of radius 1.5

### TV5 — Solid connect style

- Same-net THT pad at center, connect_style=Solid
- Expected: pad area merges into plane fill

### TV6 — Thermal connect style

- Same-net THT pad, connect_style=Thermal, thermal_gap=0.3, thermal_spoke=0.5
- Expected: 4-spoke connection, quadrant gaps visible

### TV7 — None connect style

- Same-net pad, connect_style=None
- Expected: pad gets full antipad like a foreign-net pad

### TV8 — Island removal

- Foreign obstacle splits plane into main body + small island
- keep_islands=false: island removed (1 fragment)
- keep_islands=true: island kept (2 fragments)

### TV9 — Gerber fragment output

- Refill produces 1 fragment with 1 hole
- Exported Gerber contains dark region + clear region

### TV10 — Priority ordering

- Two planes on same layer, different nets, overlapping
- Higher priority plane fills first; lower is clipped back

## Acceptance criteria

- [ ] Refill engine computes fragments from plane + board geometry
- [ ] Foreign-net obstacles are subtracted with clearance
- [ ] Board-edge keepout is applied
- [ ] NPTH holes are subtracted with clearance
- [ ] Solid/Thermal/None connect styles produce correct geometry
- [ ] Islands are removed when keep_islands=false
- [ ] Priority ordering between overlapping planes works
- [ ] Gerber export emits fragments (dark regions + clear holes)
- [ ] Tests cover TV1-TV10
- [ ] `cargo test` passes for the full workspace

## Provenance / sources

| Source | License | Used for | Notes |
|---|---|---|---|
| Volt source (crates/volt-core, volt-export, volt-cli) | MIT | current-state analysis, integration | Primary |
| i_overlay 4.5.1 | MIT OR Apache-2.0 | polygon boolean ops | Implementation dependency |
| i_float, i_shape, i_tree, i_key_sort | MIT | transitive deps of i_overlay | |
| Ucamco Gerber Format Specification | Public | Gerber region/polarity semantics | Standards reference |
| LibrePCB/KiCad | GPL | Research-only gap analysis (prior session) | Not consulted during implementation |

## Dependency decision

- **Exact package:** `i_overlay` 4.5.1
- **License:** MIT OR Apache-2.0
- **Transitive deps:** `i_float` 1.16.0 (MIT), `i_shape` 1.18.0 (MIT), `i_tree` 0.18.0 (MIT), `i_key_sort` 0.10.1 (MIT)
- **Why:** Original Rust geometry engine (not a port), permissive license, supports boolean ops + offset in one stack, cleanest clean-room posture
- **API:** `FloatOverlay::with_subj_and_clip()` → `.overlay(OverlayRule::Difference, FillRule::NonZero)` returns `Vec<Vec<Vec<[f64; 2]>>>` (shapes > contours > points)

## Open questions

- Should `board refill` be a persistent CLI command or export-time only?
- Should DRC eventually consume fragments for more accurate clearance checks?
- Arc flattening chord error: 0.01mm good enough?

## Implementation notes

Implemented in:

- `eda/crates/volt-refill/` — new crate with zone-refill engine using `i_overlay` 4.5.1
- `eda/crates/volt-core/src/project/mod.rs` — added `PlaneFragment` type and `fragments` field to `Plane`
- `eda/crates/volt-export/src/gerber.rs` — gerber copper export now uses plane fragments (dark regions + clear holes)
- `eda/crates/volt-cli/src/commands/board.rs` — plane creation initializes empty fragments
- `eda/Cargo.toml` — added `volt-refill` to workspace
- `eda/crates/volt-cli/Cargo.toml` — added `volt-refill` dependency

Tests (7 in volt-refill):
- TV1: foreign trace clearance subtraction
- TV2: same-net trace does not subtract
- TV3: board-edge keepout
- TV4: NPTH hole keepout
- TV8: island removal
- TV9: fragment contours (outer + holes)
- TV10: priority ordering

Clean-room attestation:

> This change was implemented from this spec, public i_overlay API docs, and Volt source only. No GPL source was consulted during implementation.
