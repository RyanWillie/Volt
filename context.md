# Code Context — Net-Segment Splitter (Pebble e08d3753)

## Files Retrieved
1. `eda/crates/volt-core/src/project/mod.rs` (lines 217–270) — `SchematicNetSegment`, `Junction`, `SchematicLine`, `LineEndpoint`, `NetLabel` definitions
2. `eda/crates/volt-core/src/project/mod.rs` (lines 491–530) — `BoardNetSegment`, `Trace`, `TraceEndpoint`, `Via` definitions
3. `eda/crates/volt-core/src/project/mod.rs` (lines 558–600) — `BoardPad` definition
4. `eda/crates/volt-core/src/project/mod.rs` (lines 181) — `Schematic.net_segments: Vec<SchematicNetSegment>`
5. `eda/crates/volt-core/src/project/mod.rs` (lines 295) — `Board.net_segments: Vec<BoardNetSegment>`
6. `eda/crates/volt-core/src/common/mod.rs` (line 288) — `new_uuid()` helper
7. `eda/crates/volt-cli/src/commands/schematic.rs` (lines 606–720) — `add_wire()`: finds-or-creates `SchematicNetSegment`, creates `Junction`+`SchematicLine`
8. `eda/crates/volt-cli/src/commands/board.rs` (lines 942–1108) — `board_trace()`: finds-or-creates `BoardNetSegment`, creates `Junction`+`Trace`
9. `eda/crates/volt-cli/src/commands/board.rs` (lines 1312–1520) — `board_ratsnest()`: union-find connectivity through traces/junctions/vias (the only existing graph traversal)
10. `eda/crates/volt-cli/src/commands/board.rs` (lines 1619–1639) — `endpoint_key()` and `endpoint_to_pad_index()` helpers
11. `eda/crates/volt-cli/src/commands/autoplace/tidy.rs` (lines 1–260) — Full tidy pass: dedup junctions, remove zero-length wires, merge collinear, remove orphan junctions (closest existing analog)
12. `eda/crates/volt-cli/src/commands/autoplace/tidy.rs` (lines 347–368) — `remove_orphan_junctions()`: collects referenced junctions then retains
13. `eda/crates/volt-cli/src/commands/schematic.rs` (lines 1–146) — CLI commands: **no delete/remove wire or segment command exists**
14. `eda/crates/volt-cli/src/commands/board.rs` (lines 1–204) — CLI commands: **no delete/remove trace or segment command exists**
15. `eda/crates/volt-core/tests/roundtrip.rs` (lines 280–310) — Round-trip serialization test for `SchematicNetSegment` and `BoardNetSegment`

## Key Code

### Schematic Data Model (project/mod.rs:217–270)
```rust
pub struct SchematicNetSegment {
    pub uuid: Uuid,
    pub net: Uuid,                       // reference to Circuit::nets[].uuid
    pub junctions: Vec<Junction>,        // free-floating points
    pub lines: Vec<SchematicLine>,       // wires connecting endpoints
    pub labels: Vec<NetLabel>,           // visual name labels (position-based, NOT linked to junction)
}

pub struct SchematicLine {
    pub uuid: Uuid,
    pub width: f64,
    pub from: LineEndpoint,
    pub to: LineEndpoint,
}

pub enum LineEndpoint {
    Symbol { symbol: Uuid, pin: Uuid },  // attached to a schematic symbol's pin
    Junction { junction: Uuid },          // attached to a junction node
}

pub struct Junction { pub uuid: Uuid, pub position: Position }
pub struct NetLabel { pub uuid: Uuid, pub position: Position, pub rotation: Angle, pub mirror: bool }
```

### Board Data Model (project/mod.rs:491–550)
```rust
pub struct BoardNetSegment {
    pub uuid: Uuid,
    pub net: Option<Uuid>,              // None for unconnected standalone pads
    pub traces: Vec<Trace>,             // copper trace segments
    pub vias: Vec<Via>,                 // layer-transition vias (position-based nodes)
    pub junctions: Vec<Junction>,       // generic junction nodes
    pub pads: Vec<BoardPad>,            // standalone pads (not part of a device)
}

pub struct Trace {
    pub uuid: Uuid,
    pub layer: Layer,
    pub width: f64,
    pub from: TraceEndpoint,
    pub to: TraceEndpoint,
}

pub enum TraceEndpoint {
    Device { device: Uuid, pad: Uuid },  // device footprint pad
    Via { via: Uuid },                    // via node
    Junction { junction: Uuid },          // junction node
}
```

### Existing Union-Find Connectivity (board.rs:1460–1510)
```rust
// In board_ratsnest(): builds per-trace-segment graph nodes keyed by
//   "dev:<uuid>:<pad_uuid>" | "junc:<uuid>" | "via:<uuid>"
// For each trace, unions from_key and to_key.
// Then maps device pads to their graph-node index and checks for shared roots.
fn endpoint_key(ep: &TraceEndpoint) -> String { ... }
```

### Orphan Junction Removal (tidy.rs:347–368) — Closest analog pattern
```rust
fn remove_orphan_junctions(schematic: &mut Schematic) -> usize {
    for seg in &mut schematic.net_segments {
        let mut referenced: HashSet<Uuid> = HashSet::new();
        for line in &seg.lines {
            if let LineEndpoint::Junction { junction } = &line.from { referenced.insert(*junction); }
            if let LineEndpoint::Junction { junction } = &line.to   { referenced.insert(*junction); }
        }
        seg.junctions.retain(|j| referenced.contains(&j.uuid));
    }
}
```

## Architecture

### Graph Topology

**Schematic segments** form graphs where:
- **Nodes** = `Junction` UUIDs + `Symbol{symbol,pin}` pairs
- **Edges** = `SchematicLine` (each connects two `LineEndpoint`s)
- **Labels** = free-floating by `Position`, NOT graph-connected. Must be assigned to a component by geometric proximity (closest junction/line).

**Board segments** form graphs where:
- **Nodes** = `Junction` UUIDs + `Via` UUIDs + `Device{device,pad}` pairs
- **Edges** = `Trace` (each connects two `TraceEndpoint`s)
- **Pads** = `BoardPad`s are standalone, position-based, not graph-linked to traces.

### Key Observations

1. **No delete command exists.** There is no CLI subcommand for removing a wire, trace, junction, or segment. The splitter will be invoked after the (to-be-added) delete command.

2. **Single segment per net.** Current `add_wire`/`board_trace` do `find-or-create` by net UUID — they collapse everything into one segment per net. A split is needed only after deletion breaks connectivity.

3. **Labels are positional.** `NetLabel` has no junction reference — assignment to a split component must use geometric nearest-junction/line matching.

4. **BoardPads are standalone.** `BoardPad`s sit in the segment but are not referenced by any `TraceEndpoint`. They too must be assigned by position or kept in the "primary" fragment.

5. **Vias are nodes, not edges.** `Via` is referenced *by* `TraceEndpoint::Via { via }`, similar to how `Junction` is referenced by `TraceEndpoint::Junction`. In the connectivity graph, a via is a node that multiple traces can connect to.

6. **ratsnest already has the union-find pattern** for board traces (board.rs:1460–1510), but it operates on pad-level connectivity *across* segments. The splitter needs union-find *within* a single segment.

## Splitter Design

### API Surface
```rust
// In volt-core/src/project/mod.rs or a new eda/crates/volt-core/src/project/split.rs

/// Split any schematic net segment whose lines form >1 connected component
/// into separate segments. Returns count of segments created by splitting.
pub fn split_schematic_net_segments(schematic: &mut Schematic) -> usize;

/// Split any board net segment whose traces form >1 connected component
/// into separate segments. Returns count of segments created by splitting.
pub fn split_board_net_segments(board: &mut Board) -> usize;
```

### Algorithm (Schematic)

For each `SchematicNetSegment`:

1. **Build node set.** Collect every unique `LineEndpoint` appearing in any `SchematicLine.from`/`.to`. Node identity:
   - `LineEndpoint::Junction { junction }` → key = junction UUID
   - `LineEndpoint::Symbol { symbol, pin }` → key = `(symbol, pin)` pair

2. **Union-find on edges.** For each `SchematicLine`, union its `from` node with its `to` node.

3. **Check component count.** If only 1 connected component → skip (no split needed).

4. **Partition lines.** Group lines by their connected component (root node).

5. **Partition junctions.** For each `Junction`, find which component references it (look up in the union-find by its UUID key). Orphan junctions (unreferenced) are dropped.

6. **Assign labels.** For each `NetLabel`, find the nearest junction in the segment (Euclidean distance on `position`). Assign to that junction's component. If no junctions exist in a component (pure symbol-to-symbol), assign to the component whose line endpoints are geometrically closest.

7. **Emit segments.** Keep the first component in the original segment (preserving UUID). Create new `SchematicNetSegment`s (with `new_uuid()`, same `net` UUID) for each additional component.

### Algorithm (Board)

For each `BoardNetSegment`:

1. **Build node set.** Node identity from `TraceEndpoint`:
   - `Junction { junction }` → junction UUID
   - `Via { via }` → via UUID
   - `Device { device, pad }` → `(device, pad)` pair

2. **Union-find on edges.** For each `Trace`, union `from` and `to`.

3. **Via cross-layer linking.** Vias are already nodes in the graph (referenced by `TraceEndpoint::Via`). No extra edges needed — they're connected by traces.

4. **Partition traces, junctions, vias.** Group by connected component.

5. **Assign BoardPads.** These are standalone (not in the trace graph). Assign by position: find the nearest via/junction in each component, assign pad to that component. (Or keep in the largest component as fallback.)

6. **Emit segments.** Same pattern: first component keeps original UUID; new components get `new_uuid()`, same `net`.

### Where To Put It

- **Implementation:** `eda/crates/volt-core/src/project/split.rs` (new file), with `mod split;` added to `project/mod.rs`. This keeps it in `volt-core` so both CLI and future GUI can call it.
- **Invocation:** After any wire/trace delete in the CLI (to be added), call `split_schematic_net_segments(&mut schematic)` or `split_board_net_segments(&mut board)` before writing back.
- **Also callable from tidy:** Add as step 9 in `tidy.rs` after orphan junction removal.

### Edge Cases

| Case | Handling |
|------|----------|
| Segment with 0 lines/traces after delete | Remove the segment entirely (it's empty) |
| Segment with only labels, no lines | Keep if labels exist (label-only segment is valid), or remove |
| Label equidistant to two components | Assign to the component with more lines (arbitrary tiebreak) |
| Junction referenced by 0 lines | Drop it (orphan — `remove_orphan_junctions` pattern) |
| Via referenced by 0 traces | Keep it (vias have physical meaning) or drop — match LibrePCB behavior |
| All traces on same component | No-op, no split needed |
| Net with multiple segments already | Each segment is split independently; net UUID preserved on all |

### Test Plan (per acceptance criteria)

1. Create a 3-wire linear segment: `A—J1—B—J2—C` (junctions J1, J2, lines A↔J1, J1↔J2, J2↔C)
2. Delete the middle wire (J1↔J2)
3. Call `split_schematic_net_segments`
4. Assert: 2 segments exist for that net, with disjoint junction sets `{J1}` and `{J2}`
5. Assert: round-trip through JSON is stable (serialize → deserialize → identical)

Same pattern for board with traces/vias.

## Start Here

Start at **`eda/crates/volt-core/src/project/mod.rs` line 217** — the `SchematicNetSegment` struct definition. This is the data model you're splitting. Then read `BoardNetSegment` at line 491. The `board_ratsnest` function at `board.rs:1430` has the only existing union-find pattern to use as reference. The `tidy.rs` file shows the mutation patterns (retain, remove, remap) you'll reuse.
