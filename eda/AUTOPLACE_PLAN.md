# Autoplace & Tidy Algorithm — Redesign Plan

## Current Problems

The existing algorithm has fundamental limitations:

1. **Single-anchor star topology** — Picks one "most connected" component as center, radiates everything outward in 4 zones. This only works for simple circuits with one dominant IC. Fails for: multi-IC designs, cascaded stages, parallel signal paths, circuits with no clear center.

2. **No signal flow awareness** — Components are grouped by raw connectivity, not by input→output direction. A resistor on the input and a resistor on the output both end up in the same zone because they connect to the same IC.

3. **Fixed spacing** — Hard-coded 3.5/4.0/5.0 grid spacing regardless of actual symbol sizes. An 8-pin IC and a 64-pin MCU get the same spacing, causing overlaps on large symbols and wasted space on small ones.

4. **Greedy chain following** — Takes the first unvisited neighbor, not the best one. Creates chains like `MCU → LED → UART_TX_resistor` instead of following actual signal paths.

5. **No component classification** — Bypass capacitors, pull-up resistors, and power supply components are all treated identically. A bypass cap connected to VCC and GND gets dumped in the "power row" instead of placed near its IC.

6. **Naive wiring** — Sorts pin endpoints by X coordinate and chains them with wires. Creates unnecessary crossings. No Steiner tree or minimum spanning tree optimization.

7. **No rotation optimization** — All components placed at 0° regardless of pin orientation vs. connection direction.

8. **Tidy is surface-level** — Only does junction dedup, zero-length wire removal, label nudging, and translation. Doesn't fix overlaps, doesn't improve component order, doesn't reroute wires.

---

## Design Goals

The new algorithm must produce schematics that follow professional conventions:

- **Signal flows left-to-right**: inputs on the left, processing in the middle, outputs on the right
- **Power flows top-to-bottom**: VCC/supply at top, GND at bottom
- **Related components cluster together**: bypass cap next to its IC, LED next to its series resistor
- **No overlaps**: components and wires respect each other's space
- **Minimal wire crossings**: readable nets with clear visual paths
- **Grid-aligned**: everything snaps to the 2.54mm schematic grid
- **Works at any scale**: from a 3-component LED circuit to a 50+ component mixed-signal board

---

## Algorithm Overview

A **Sugiyama-style layered graph drawing** algorithm, adapted for schematic conventions. Eight phases:

```
┌─────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│  1. Analyze  │──▶│  2. Rank      │──▶│  3. Order    │──▶│  4. Orient   │
│  (classify)  │   │  (columns)   │   │  (rows)      │   │  + Position  │
└─────────────┘   └──────────────┘   └──────────────┘   └──────────────┘
                                                                │
┌─────────────┐   ┌──────────────┐   ┌──────────────┐   ┌──────────────┐
│  8. Tidy     │◀──│  7. Texts    │◀──│  6. Wire    │◀──│  5. Attach   │◀───┘
│  (cleanup)   │   │  (fields)    │   │  + Labels   │   │  (specials)  │
└─────────────┘   └──────────────┘   └──────────────┘   └──────────────┘
```

---

## Phase 1: Analysis & Classification

### 1.1 Build Connectivity Graph

From the `Circuit`, build an undirected graph where:
- **Nodes** = `ComponentInstance` UUIDs
- **Edges** = shared nets (excluding power nets)

Also build:
- `net_members: HashMap<Uuid, Vec<Uuid>>` — which components connect to each net
- `comp_nets: HashMap<Uuid, Vec<Uuid>>` — which nets each component connects to

### 1.2 Classify Nets

```rust
enum NetClass {
    Power,      // VCC, GND, 3V3, etc. (existing is_power_net_name logic, extended)
    Signal,     // Everything else by default
    HighFanout, // Signal nets with > 4 connections (treat like power for wiring)
}
```

### 1.3 Classify Components

Using library `SignalRole` data + connectivity patterns + name heuristics:

```rust
enum ComponentRole {
    /// Has only outputs on signal nets, or is a power source (battery, regulator output).
    /// Examples: voltage regulator, oscillator, sensor output
    Source,
    
    /// Has only inputs on signal nets, or is a power sink.
    /// Examples: LED, motor driver, display
    Sink,
    
    /// Has both input and output signals. The "main" processing components.
    /// Examples: MCU, op-amp, logic gate, comparator
    Processor,
    
    /// 2-pin passive sitting on a signal path between two other components.
    /// Examples: series resistor, coupling capacitor, ferrite bead
    SeriesPassive,
    
    /// Connected between a power net and a signal net (or two power nets).
    /// Examples: bypass/decoupling cap, bulk cap
    BypassPassive,
    
    /// Connected between a power net and a signal net, but not in-series.
    /// Examples: pull-up resistor, pull-down resistor
    ShuntPassive,
    
    /// Power supply chain component (connected only to power nets).
    /// Examples: battery, power switch, voltage regulator (when all connections are power)
    PowerChain,
}
```

**Classification logic:**

```
For each component:
  signal_inputs  = signals with role Input connected to signal nets
  signal_outputs = signals with role Output connected to signal nets  
  power_pins     = signals connected to power nets
  passive_pins   = signals with role Passive
  
  if all connected nets are power → PowerChain
  if is 2-pin passive:
    if both pins on same signal net → (skip, weird)
    if one pin on power net, one on signal net:
      count other components on the signal net
      if signal net has other components → ShuntPassive (pull-up/pull-down)
      else → BypassPassive (decoupling)
    if one pin on power net, other on power net → BypassPassive
    if both pins on signal nets → SeriesPassive
  if signal_outputs > 0 && signal_inputs == 0 → Source
  if signal_inputs > 0 && signal_outputs == 0 → Sink
  if signal_inputs > 0 && signal_outputs > 0 → Processor
  else → use designator prefix heuristic (U=Processor, J=Source/Sink, etc.)
```

### 1.4 Build Directed Signal Flow Graph

Create a DAG from signal connections:
- For each signal net, identify the **driver** (Output/Bidirectional pin) and **receivers** (Input/Passive pins)
- Add directed edges: driver_component → receiver_component
- For passive chains (SeriesPassive), direction is inherited from the upstream component
- Break cycles by removing the back-edge with the lowest "confidence" (bidirectional pins are less certain)

### 1.5 Detect Companion Components

Build a map of "attached" components that should be placed near a parent:
- **Bypass caps**: a 2-pin passive where one pin is on a power net shared with an IC, and the other pin is on GND → attach to that IC
- **Pull-up/pull-down resistors**: one pin on power/GND, other pin on a signal net that connects to a specific IC → attach to that IC's pin
- **LED + series resistor pairs**: LED connected to a resistor, resistor connected to an IC → group them

```rust
struct Companion {
    component: Uuid,      // the bypass cap / pull-up / LED
    parent: Uuid,         // the IC it's attached to
    attachment: AttachmentType,
}

enum AttachmentType {
    Bypass,              // place near parent, close to power pin
    PullUp,              // place above the signal pin
    PullDown,            // place below the signal pin  
    SeriesLED,           // place inline on the signal path
}
```

Companion components are **excluded from the main ranking** and placed in Phase 5.

---

## Phase 2: Rank Assignment (Column/Layer)

Assign each non-companion component to a horizontal **rank** (column). Rank 0 is leftmost.

### 2.1 Initial Ranking: Longest-Path from Sources

```
1. Find all Source components → assign rank 0
2. BFS/DFS along signal flow edges:
   rank[v] = max(rank[u] + 1) for all predecessors u of v
3. If no sources found (e.g., purely passive circuit):
   pick the component with the most connections as rank 0
   BFS outward assigning increasing ranks
```

### 2.2 Promote Sinks to Maximum Rank

All Sink components are moved to `max_rank` to ensure they appear on the right edge.

### 2.3 Rank Compaction

If any rank is empty (no components), shift subsequent ranks left to fill the gap.

### 2.4 Power Chain Handling

PowerChain components get a special rank: `rank = -1` (placed above the main flow as a dedicated power supply row). They are ranked among themselves by connectivity order (battery → switch → regulator → filter cap).

---

## Phase 3: Ordering Within Ranks (Row Assignment)

For each rank, determine the vertical order of components to **minimize edge crossings**.

### 3.1 Barycenter Heuristic

For each component in rank `r`, compute its **barycenter** — the average position of its neighbors in the adjacent rank:

```
barycenter(v, r) = average(position_in_rank[u] for u in neighbors(v) ∩ rank[r±1])
```

Sort components within each rank by their barycenter value.

### 3.2 Multi-Pass Sweep

Repeat the following until convergence (or max 24 iterations):
1. **Forward sweep** (rank 0 → max_rank): order each rank by barycenter of left neighbors
2. **Backward sweep** (max_rank → rank 0): order each rank by barycenter of right neighbors
3. Count total edge crossings — stop if no improvement

### 3.3 Initial Ordering Seed

For the first pass (before any barycenters exist), order components within each rank by:
1. Number of connections to the previous rank (descending)
2. Then by component name (alphabetical) for stability

---

## Phase 4: Orientation & Coordinate Assignment

Convert abstract (rank, order) positions to actual (x, y) coordinates with correct component rotation.

### 4.1 Component Orientation (Rotation + Mirror)

This is critical for readable schematics. Every component must be rotated so its pins face toward their connections.

**How symbol pins work in the data model:**
- `pin.position` = the wire connection point (tip of the pin stub)
- `pin.rotation` = direction the pin extends *toward the body* (0° = rightward, 180° = leftward)
- So a pin at `(-5.08, 0)` with rotation 0° is a **left-side pin** (wire connects on the left)
- A pin at `(5.08, 0)` with rotation 180° is a **right-side pin** (wire connects on the right)
- When symbol rotation is applied, all pin positions and directions rotate with it

**Step 1: Analyze the symbol's pin layout (at 0° rotation)**

For each symbol, classify each pin by the side of the body it's on:

```rust
enum PinSide { Left, Right, Top, Bottom }

fn classify_pin_side(pin: &SymbolPin) -> PinSide {
    // Pin rotation tells us which way the stub points toward the body
    match normalize_angle(pin.rotation.0) {
        0   => PinSide::Left,    // stub goes right → wire connects on left
        90  => PinSide::Bottom,  // stub goes up → wire connects on bottom  
        180 => PinSide::Right,   // stub goes left → wire connects on right
        270 => PinSide::Top,     // stub goes down → wire connects on top
        _   => /* nearest 90° */ 
    }
}
```

Build a `SymbolPinProfile` for each component:
```rust
struct SymbolPinProfile {
    left_pins:   Vec<(Uuid, SignalRole)>,  // pins whose wires extend leftward
    right_pins:  Vec<(Uuid, SignalRole)>,  // pins whose wires extend rightward
    top_pins:    Vec<(Uuid, SignalRole)>,  // pins whose wires extend upward
    bottom_pins: Vec<(Uuid, SignalRole)>,  // pins whose wires extend downward
}
```

**Step 2: Determine ideal pin orientation based on signal flow**

For each component, determine which side should face which direction:

| Component Role | Ideal Orientation (at 0° rotation) |
|---|---|
| **Processor** (IC with I/O) | Input pins face left (toward sources), Output pins face right (toward sinks) |
| **Source** | Output pins face right |
| **Sink** | Input pins face left |
| **SeriesPassive** (in signal chain) | Pin 1 faces left (toward source), Pin 2 faces right (toward sink) — i.e., horizontal |
| **ShuntPassive** (pull-up) | Pin 1 faces up (toward VCC), Pin 2 faces down (toward signal) — i.e., vertical |
| **ShuntPassive** (pull-down) | Pin 1 faces up (toward signal), Pin 2 faces down (toward GND) — i.e., vertical |
| **BypassPassive** (decoupling cap) | Horizontal next to IC, or vertical between power rails |
| **PowerChain** | Signal flow direction through the power chain (left-to-right) |

**Step 3: Score each candidate rotation**

For each component, try all 4 rotations (0°, 90°, 180°, 270°) and optionally mirror. Score each:

```rust
fn score_rotation(component, rotation, mirror, neighbors, placements) -> f64 {
    let mut score = 0.0;
    
    for (pin, connected_component) in pin_connections {
        let pin_world_pos = transform_pin(pin, rotation, mirror, component_pos);
        let pin_wire_direction = wire_direction_after_rotation(pin, rotation, mirror);
        let neighbor_pos = placements[connected_component];
        
        // Direction from this component to the connected component
        let dx = neighbor_pos.x - component_pos.x;
        let dy = neighbor_pos.y - component_pos.y;
        let ideal_direction = (dx, dy).normalize();
        
        // How well does this pin's wire direction match the ideal direction?
        let alignment = dot_product(pin_wire_direction, ideal_direction);
        score += alignment;  // +1.0 for perfect alignment, -1.0 for opposite
    }
    
    // Bonus: prefer 0° for multi-pin ICs (convention)
    if pin_count > 4 && rotation == 0.0 { score += 0.5; }
    
    // Bonus: prefer inputs on left, outputs on right at 0° if symbol is designed that way
    if rotation == 0.0 && symbol_has_inputs_left_outputs_right { score += 1.0; }
    
    score
}
```

Choose the rotation with the highest score. Ties broken by preferring 0° > 90° > 270° > 180°.

**Step 4: Special cases**

- **2-pin passives in horizontal signal chain**: Always 0° (pin 1 left, pin 2 right). If signal flows right-to-left locally, use 180°.
- **2-pin passives as vertical shunt (pull-up/pull-down)**: Always 90° (pins top/bottom).
- **ICs imported from KiCad/LibrePCB**: Usually already designed with inputs on left, outputs on right at 0° — prefer keeping 0°.
- **Connectors**: Orient so pins face outward (toward the edge of the schematic).

### 4.2 X Coordinate (Rank → Column Position)

```
For each rank r:
  column_x[r] = column_x[r-1] + max_width_in_rank[r-1]/2 + gap + max_width_in_rank[r]/2

Where:
  max_width_in_rank[r] = max(symbol_width(c) for c in rank[r])
  gap = base_gap + wire_density_bonus
  base_gap = 6 grid units (15.24mm) — enough for labels and short wires
  wire_density_bonus = 1 grid unit per net crossing between ranks r-1 and r
```

### 4.3 Y Coordinate (Order → Row Position)

```
For each component c at order i in rank r:
  Within a rank, stack components vertically:
  row_y[r][i] = row_y[r][i-1] + height_of(comp[i-1])/2 + gap + height_of(comp[i])/2

Where:
  height_of(c) = symbol_height from actual library symbol extents (AFTER rotation applied)
  gap = 3 grid units (7.62mm) minimum, increased if component texts extend further
```

### 4.4 Vertical Centering

After positioning, shift each rank vertically so its center aligns with the median y-position of its neighbors. This prevents the layout from drifting upward or downward.

### 4.5 Grid Snapping

All final positions are snapped to the nearest grid point (2.54mm).

---

## Phase 5: Companion Attachment

Place companion components relative to their parent:

### 5.1 Bypass Capacitors
- Find the parent IC's power pin position
- Place the bypass cap **directly above or below** the IC, close to the power pin
- Orient horizontally with pin 1 toward VCC and pin 2 toward GND
- If multiple bypass caps for one IC, stack them vertically

### 5.2 Pull-Up Resistors
- Find the signal pin on the parent IC where the pull-up connects
- Place the resistor **vertically above** that pin position
- Orient at 90° with pin 1 (top) toward VCC, pin 2 (bottom) toward the signal net

### 5.3 Pull-Down Resistors
- Same as pull-up but **vertically below**, pin 2 toward GND

### 5.4 Series LED + Resistor
- Place inline with the signal path, between the IC and the next component
- LED and resistor adjacent horizontally

### 5.5 Overlap Resolution
After placing companions, check for overlaps with main components and other companions. If overlapping, nudge outward (away from the parent IC body).

---

## Phase 6: Wiring & Label Placement

This phase handles wiring AND all label/text placement together, because they interact — labels must not overlap wires, components, or each other.

### 6.1 Power Nets — Labels Only
For each power net (VCC, GND, 3V3, etc.):
- Place a **net label** at each pin endpoint
- No wires drawn (standard schematic convention for power rails)
- **Label position rule**: offset from pin by 1 grid unit in the pin's wire direction (away from body)
- **Label rotation rule**:
  - Pin faces left or right → label rotation 0° (horizontal text)
  - Pin faces up → label rotation 90° 
  - Pin faces down → label rotation 270°
- **VCC-family labels**: place above pin (offset -Y) 
- **GND-family labels**: place below pin (offset +Y)

### 6.2 Signal Nets — Minimum Spanning Tree Routing
For each signal net:
1. Collect all pin world-positions for this net
2. If 2 endpoints: direct Manhattan wire (horizontal-first or vertical-first, whichever has fewer crossings with existing wires)
3. If 3+ endpoints: build a **rectilinear Steiner minimum spanning tree** (approximate):
   - Start with the pin closest to the median position as the "trunk"
   - Add Steiner points (junctions) at optimal meeting points
   - Wire each pin to the nearest point on the trunk
4. Use Manhattan routing (horizontal + vertical segments only)

### 6.3 Net Label Placement on Signal Wires

Every signal net with wires gets exactly ONE label. Placement algorithm:

```
1. Find the longest horizontal wire segment in the net's routing
2. Place label at the midpoint of that segment
3. Offset label position 1 grid unit ABOVE the wire (negative Y)
4. Label rotation = 0° (horizontal text for horizontal wires)
5. If the longest segment is vertical instead:
   - Place at midpoint, offset 1 grid unit to the LEFT
   - Label rotation = 90°
```

If the chosen position would overlap with a component body:
- Try the opposite side (below instead of above, right instead of left)
- If both sides overlap, try the next-longest segment
- As a last resort, place at the wire endpoint closest to the midpoint of the net

### 6.4 High-Fanout Signal Nets
Signal nets with > 4 connections: treat like power nets (labels only, no wires) to avoid spaghetti.

### 6.5 Wire Crossing Minimization
After initial routing, count crossing wires. For each crossing pair, try:
- Swapping the bend direction (horizontal-first ↔ vertical-first)
- Using the alternative that reduces total crossings

---

## Phase 7: Component Text Field Placement

Every placed symbol has text fields (NAME like "R1", VALUE like "10k") that come from the library symbol templates. These need collision-aware positioning.

### 7.1 How text fields work

- Library `SymbolText` defines a template: layer (SchNames/SchValues), offset from symbol origin, rotation, height, alignment
- When placing a symbol, `SchematicText` is created by adding template offset to symbol world position
- Template positions are designed for 0° rotation — when the symbol is rotated, text positions must be recalculated

### 7.2 Text field positioning algorithm

**Step 1: Start from library template position**

For each text field, compute the default world position:
```
text_pos = symbol_pos + rotate(template_offset, symbol_rotation)
text_rotation = template_rotation + symbol_rotation
```

**Step 2: Compute text bounding box**

Estimate the text extent (we don't have font metrics, so approximate):
```
text_width  ≈ character_count × height × 0.6  (monospace approximation)
text_height ≈ height × 1.2
```

Using the text's alignment anchor (h_align, v_align), compute the bounding box:
```
For HAlign::Left:   bbox_x0 = text_pos.x
For HAlign::Center: bbox_x0 = text_pos.x - text_width/2
For HAlign::Right:  bbox_x0 = text_pos.x - text_width
(similar for vertical)
```

**Step 3: Check for collisions**

Check the text bbox against:
- All component body bounding boxes (from symbol polygons + pins)
- All other text field bounding boxes already placed
- All wire segments
- All net label positions

**Step 4: Resolve collisions**

If the default position collides:
1. Try the **opposite side** of the component body:
   - NAME default is above → try below
   - VALUE default is below → try above
   - If component is rotated 90°, try left/right instead
2. Try **sliding along the component edge** (move text left/right or up/down along the body)
3. If all positions collide, use the position with the **least overlap area** and flag for tidy cleanup

**Step 5: Substitute template values**

Replace `{{NAME}}` with the component designator and `{{VALUE}}` with the component value.

### 7.3 Multi-gate components

Components with multiple gates (e.g., dual op-amp, quad NAND) each gate is a separate symbol. Each gate's text fields are placed independently, but only one gate shows the NAME/VALUE (others show the gate suffix like "A", "B").

---

## Phase 8: Tidy (Enhanced)

The tidy command improves an **existing** layout without regenerating it.

### 8.1 Existing Operations (keep)
- Dedup junctions
- Remove zero-length wires
- Snap to grid

### 8.2 Component Overlap Resolution

```
1. Compute bounding boxes for all placed components
   Include the symbol body (polygons + pins) AND text fields (NAME, VALUE)
   Add 1 grid unit of padding around each box

2. For each overlapping pair (A, B):
   a. Compute overlap rectangle
   b. Determine separation axis (X or Y) with least required movement
   c. Move the SMALLER component (fewer pins) outward along that axis
   d. Snap to grid after moving
   e. Update the component's text field positions (maintain relative offset)

3. Repeat until no overlaps remain (max 20 iterations to prevent infinite loops)
```

### 8.3 Text Field Collision Resolution  

```
1. Collect all text field bounding boxes (NAME + VALUE for every component)
   Also collect all net label bounding boxes

2. For each text-vs-text overlap:
   Try these positions in order:
   a. Slide along the component edge (±1 grid unit)
   b. Move to opposite side of component body
   c. Offset further outward from body
   Pick first position with no collisions

3. For each text-vs-component-body overlap:
   The text must move (not the component)
   Same candidate positions as above
```

### 8.4 Wire Cleanup
- Detect redundant junctions (junction with exactly 2 collinear wires → merge into one wire)
- Remove dog-leg bends where a straight wire would work
- Merge collinear wire segments
- Re-route wires to moved components

### 8.5 Net Label Cleanup

```
1. Remove duplicate labels on the same net segment
2. Check every label against every component body bbox:
   If overlapping → move label to nearest non-overlapping grid position
   Prefer: above the wire, then below, then left, then right
3. Check every label against every other label:
   If overlapping → nudge the lower-priority one (further from wire midpoint)
   by 2.5 grid units in the direction with more space
4. Ensure every wired net has at least one label
5. Remove labels on nets that are label-only (power nets with no wires — 
   labels are the wires, so they must stay)
```

### 8.6 Alignment Improvement
- Find components that are "almost aligned" (within 1 grid unit of same X or Y)
- Snap them to exact alignment if it doesn't cause overlaps

### 8.7 Spacing Normalization
- If components in a row have irregular spacing, normalize to consistent gaps
- But only if it doesn't increase total wire length significantly

### 8.8 Compact Layout
- Translate entire layout so minimum position is at (8, 8) grid units from origin
- Existing logic, kept as final step

---

## Implementation Strategy

### File Structure
Keep everything in `autoplace.rs` but reorganize into clear sections with helper structs:

```rust
// autoplace.rs

// --- Data structures ---
enum NetClass { Power, Signal, HighFanout }
enum ComponentRole { Source, Sink, Processor, SeriesPassive, BypassPassive, ShuntPassive, PowerChain }
enum AttachmentType { Bypass, PullUp, PullDown, SeriesLED }
struct Companion { component: Uuid, parent: Uuid, attachment: AttachmentType }
struct LayoutGraph { /* adjacency, ranks, orders */ }
enum PinSide { Left, Right, Top, Bottom }
struct SymbolPinProfile { left: Vec<...>, right: Vec<...>, top: Vec<...>, bottom: Vec<...> }
struct TextBBox { x0: f64, y0: f64, x1: f64, y1: f64 }

// --- Phase 1: Analysis ---
fn classify_nets(circuit, net_members) -> HashMap<Uuid, NetClass>
fn classify_components(circuit, net_classes, lib_comps, lib_syms) -> HashMap<Uuid, ComponentRole>
fn build_flow_dag(circuit, net_classes, roles) -> DirectedGraph
fn detect_companions(circuit, roles, net_classes) -> Vec<Companion>

// --- Phase 2: Ranking ---
fn assign_ranks(dag, roles) -> HashMap<Uuid, i32>
fn compact_ranks(ranks) -> HashMap<Uuid, i32>

// --- Phase 3: Ordering ---
fn initial_order(ranks, dag) -> HashMap<i32, Vec<Uuid>>
fn barycenter_ordering(rank_order, dag, max_iterations) -> HashMap<i32, Vec<Uuid>>

// --- Phase 4: Orientation & Positioning ---
fn analyze_pin_profile(symbol) -> SymbolPinProfile
fn score_rotation(component, rotation, mirror, neighbors, positions) -> f64
fn choose_best_rotation(component, neighbors, positions, sym) -> (f64, bool)  // (rotation, mirror)
fn assign_coordinates(rank_order, sym_extents, dag) -> HashMap<Uuid, (f64, f64, f64)>  // (x, y, rotation)
fn center_ranks(positions, rank_order)
fn snap_to_grid(positions)

// --- Phase 5: Companions ---
fn place_companions(companions, positions, sym_extents, circuit, lib_data) -> HashMap<Uuid, (f64, f64, f64)>
fn resolve_overlaps(all_positions, sym_extents)

// --- Phase 6: Wiring & Labels ---
fn route_power_nets(circuit, net_classes, sym_positions, lib_data) -> Vec<SchematicNetSegment>
fn route_signal_nets(circuit, net_classes, sym_positions, lib_data) -> Vec<SchematicNetSegment>
fn place_signal_net_label(net_segment, comp_boxes) -> NetLabel
fn place_power_net_labels(pin_positions, pin_directions) -> Vec<NetLabel>

// --- Phase 7: Text Fields ---
fn compute_text_bbox(text, display_value) -> TextBBox
fn place_component_texts(sym, lib_sym, comp_name, comp_value, all_boxes) -> Vec<SchematicText>
fn resolve_text_collisions(all_texts, comp_boxes, wire_segments)

// --- Phase 8: Tidy ---
fn tidy_resolve_overlaps(schematic, circuit, lib_data)
fn tidy_resolve_text_collisions(schematic, circuit, lib_data)
fn tidy_cleanup_wires(schematic)
fn tidy_cleanup_labels(schematic, comp_boxes)
fn tidy_align_components(schematic)
fn tidy_compact(schematic)
```

### Rotation Strategy

Fully covered in Phase 4.1 above. The key principle: every component is scored across all candidate rotations (0°/90°/180°/270° × mirror) based on how well its pin wire-directions align with the actual positions of connected neighbors. The highest-scoring rotation wins.

### Testing Approach

Test with progressively complex circuits:
1. **Simple**: LED + resistor + battery (3 components)
2. **Linear chain**: Sensor → amp → filter → ADC (4 components, 1 chain)
3. **Star**: MCU with 6 peripherals (passives, LEDs, connectors)
4. **Mixed**: Power supply + MCU + peripherals + bypass caps (15+ components)
5. **Large**: Full Arduino-class board (30+ components, multiple ICs)

For each test case, render SVG and visually verify:
- No overlaps
- Signal flows left-to-right
- Power rails at top/bottom
- Bypass caps near their ICs
- Minimal wire crossings
- Labels readable and non-overlapping
