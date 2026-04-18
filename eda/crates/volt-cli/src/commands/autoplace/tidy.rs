//! Phase 8 — Tidy pass for schematic cleanup.
//!
//! Performs post-placement/wiring cleanup operations:
//! 1. Dedup junctions at the same position
//! 2. Remove zero-length wires
//! 3. Merge collinear wire segments
//! 4. Snap everything to grid
//! 5. Remove orphan junctions
//! 6. Fix label-component overlaps
//! 7. Spread overlapping labels
//! 8. Compact layout to a consistent origin

use std::collections::HashMap;
use std::path::Path;
use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Component, Symbol};
use volt_core::project::*;

use super::types::*;

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

// ===========================================================================
// Public API
// ===========================================================================

/// Run all tidy operations on a schematic, returning a list of change descriptions.
pub fn tidy_pass(
    schematic: &mut Schematic,
    circuit: &Circuit,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &HashMap<Uuid, Symbol>,
    _project: &Path,
) -> Result<Vec<String>> {
    let mut changes: Vec<String> = Vec::new();

    // 1. Dedup junctions
    let dedup_count = dedup_junctions(schematic);
    if dedup_count > 0 {
        changes.push(format!("Removed {} duplicate junction(s)", dedup_count));
    }

    // 2. Remove zero-length wires
    let zero_count = remove_zero_length_wires(schematic);
    if zero_count > 0 {
        changes.push(format!("Removed {} zero-length wire(s)", zero_count));
    }

    // 3. Merge collinear segments
    let merge_count = merge_collinear_segments(schematic);
    if merge_count > 0 {
        changes.push(format!("Merged {} collinear segment pair(s)", merge_count));
    }

    // 4. Snap to grid
    let snap_count = snap_all_to_grid(schematic);
    if snap_count > 0 {
        changes.push(format!("Snapped {} element(s) to grid", snap_count));
    }

    // 5. Remove orphan junctions
    let orphan_count = remove_orphan_junctions(schematic);
    if orphan_count > 0 {
        changes.push(format!("Removed {} orphan junction(s)", orphan_count));
    }

    // 6. Fix label-component overlaps
    let comp_boxes = compute_comp_boxes(schematic, circuit, lib_comps, lib_syms);
    let overlap_count = fix_label_component_overlaps(schematic, &comp_boxes);
    if overlap_count > 0 {
        changes.push(format!(
            "Fixed {} label-component overlap(s)",
            overlap_count
        ));
    }

    // 7. Spread overlapping labels
    let nudge_count = spread_overlapping_labels(schematic);
    if nudge_count > 0 {
        changes.push(format!("Nudged {} overlapping label(s)", nudge_count));
    }

    // 8. Compact layout
    let compacted = compact_layout(schematic);
    if compacted {
        changes.push("Compacted layout to origin".into());
    }

    Ok(changes)
}

// ===========================================================================
// 1. Dedup junctions
// ===========================================================================

/// Remove duplicate junctions at the same position within each net segment.
/// Positions are rounded to 0.001 mm for comparison. The first junction at a
/// position is kept; later duplicates are removed and line references remapped.
fn dedup_junctions(schematic: &mut Schematic) -> usize {
    let mut total_removed = 0;

    for seg in &mut schematic.net_segments {
        // Map rounded position → first junction UUID seen at that position.
        let mut seen: HashMap<(i64, i64), Uuid> = HashMap::new();
        // Duplicate UUID → canonical UUID it should be replaced with.
        let mut remap: HashMap<Uuid, Uuid> = HashMap::new();

        for junc in &seg.junctions {
            let key = round_pos_key(junc.position.x, junc.position.y);
            match seen.get(&key) {
                Some(&canonical) => {
                    remap.insert(junc.uuid, canonical);
                }
                None => {
                    seen.insert(key, junc.uuid);
                }
            }
        }

        if remap.is_empty() {
            continue;
        }

        // Remap line endpoints.
        for line in &mut seg.lines {
            remap_endpoint(&mut line.from, &remap);
            remap_endpoint(&mut line.to, &remap);
        }

        // Remove duplicate junctions.
        let before = seg.junctions.len();
        seg.junctions.retain(|j| !remap.contains_key(&j.uuid));
        total_removed += before - seg.junctions.len();
    }

    total_removed
}

// ===========================================================================
// 2. Remove zero-length wires
// ===========================================================================

/// Remove wires where `from == to` (LineEndpoint derives PartialEq).
fn remove_zero_length_wires(schematic: &mut Schematic) -> usize {
    let mut total = 0;
    for seg in &mut schematic.net_segments {
        let before = seg.lines.len();
        seg.lines.retain(|line| line.from != line.to);
        total += before - seg.lines.len();
    }
    total
}

// ===========================================================================
// 3. Merge collinear segments
// ===========================================================================

/// For each junction referenced by exactly 2 lines (both Junction↔Junction),
/// if the two segments are collinear (both horizontal or both vertical),
/// merge them into one line and remove the shared junction.
fn merge_collinear_segments(schematic: &mut Schematic) -> usize {
    let mut total_merged = 0;

    for seg in &mut schematic.net_segments {
        // May need multiple passes until no more merges are possible.
        loop {
            let merged_one = try_merge_one(seg);
            if merged_one {
                total_merged += 1;
            } else {
                break;
            }
        }
    }

    total_merged
}

/// Attempt a single merge within one net segment. Returns true if a merge occurred.
fn try_merge_one(seg: &mut SchematicNetSegment) -> bool {
    // Build junction reference counts: junction UUID → list of line indices.
    let mut junc_refs: HashMap<Uuid, Vec<usize>> = HashMap::new();
    for (i, line) in seg.lines.iter().enumerate() {
        if let LineEndpoint::Junction { junction } = &line.from {
            junc_refs.entry(*junction).or_default().push(i);
        }
        if let LineEndpoint::Junction { junction } = &line.to {
            junc_refs.entry(*junction).or_default().push(i);
        }
    }

    // Snapshot junction data so we don't borrow seg.junctions during mutation.
    let junc_snapshot: Vec<(Uuid, f64, f64)> = seg
        .junctions
        .iter()
        .map(|j| (j.uuid, j.position.x, j.position.y))
        .collect();

    for &(junc_uuid, junc_x, junc_y) in &junc_snapshot {
        let Some(line_indices) = junc_refs.get(&junc_uuid) else {
            continue;
        };
        // Deduplicate indices (a line can reference the same junction in both endpoints).
        let mut unique: Vec<usize> = line_indices.clone();
        unique.sort_unstable();
        unique.dedup();
        if unique.len() != 2 {
            continue;
        }

        let (i0, i1) = (unique[0], unique[1]);
        let line_a = &seg.lines[i0];
        let line_b = &seg.lines[i1];

        // Both endpoints on both lines must be Junction (skip Symbol endpoints).
        let (ep_a_other, ep_b_other) = match (
            other_junction_endpoint(line_a, junc_uuid),
            other_junction_endpoint(line_b, junc_uuid),
        ) {
            (Some(a), Some(b)) => (a, b),
            _ => continue,
        };

        // Resolve positions.
        let mid = (junc_x, junc_y);
        let pos_a = match junction_position(ep_a_other, &seg.junctions) {
            Some(p) => p,
            None => continue,
        };
        let pos_b = match junction_position(ep_b_other, &seg.junctions) {
            Some(p) => p,
            None => continue,
        };

        // Check collinearity: both horizontal or both vertical.
        let both_h = approx_eq(pos_a.1, mid.1) && approx_eq(pos_b.1, mid.1);
        let both_v = approx_eq(pos_a.0, mid.0) && approx_eq(pos_b.0, mid.0);
        if !both_h && !both_v {
            continue;
        }

        // Merge: keep line at i0, connect outer endpoints, remove line at i1 and junction.
        let width = seg.lines[i0].width;
        let new_from = LineEndpoint::Junction {
            junction: ep_a_other,
        };
        let new_to = LineEndpoint::Junction {
            junction: ep_b_other,
        };

        seg.lines[i0] = SchematicLine {
            uuid: seg.lines[i0].uuid,
            width,
            from: new_from,
            to: new_to,
        };

        // Remove line at i1 (careful: if i1 > i0, index is still valid).
        seg.lines.remove(i1);

        // Remove the shared junction.
        seg.junctions.retain(|j| j.uuid != junc_uuid);

        return true;
    }

    false
}

/// Given a line and a junction UUID that appears in one of its endpoints,
/// return the *other* endpoint's junction UUID. Returns None if the other
/// endpoint is a Symbol or if the junction doesn't appear in the line.
fn other_junction_endpoint(line: &SchematicLine, junc_uuid: Uuid) -> Option<Uuid> {
    let from_match =
        matches!(&line.from, LineEndpoint::Junction { junction } if *junction == junc_uuid);
    let to_match =
        matches!(&line.to, LineEndpoint::Junction { junction } if *junction == junc_uuid);

    if from_match && !to_match {
        if let LineEndpoint::Junction { junction } = &line.to {
            return Some(*junction);
        }
    } else if to_match && !from_match {
        if let LineEndpoint::Junction { junction } = &line.from {
            return Some(*junction);
        }
    }
    // Both endpoints are the same junction (degenerate) or not a junction endpoint.
    None
}

/// Look up a junction's position by UUID.
fn junction_position(junc_uuid: Uuid, junctions: &[Junction]) -> Option<(f64, f64)> {
    junctions
        .iter()
        .find(|j| j.uuid == junc_uuid)
        .map(|j| (j.position.x, j.position.y))
}

// ===========================================================================
// 4. Snap to grid
// ===========================================================================

/// Snap all symbol positions, text positions, junction positions, and label
/// positions to the nearest GRID (2.54 mm) multiple. Returns how many
/// elements were moved.
fn snap_all_to_grid(schematic: &mut Schematic) -> usize {
    let mut count = 0;

    // Symbols and their texts.
    for sym in &mut schematic.symbols {
        if snap_position(&mut sym.position) {
            count += 1;
        }
        for txt in &mut sym.texts {
            if snap_position(&mut txt.position) {
                count += 1;
            }
        }
    }

    // Junctions, lines (no positions to snap on lines), labels.
    for seg in &mut schematic.net_segments {
        for junc in &mut seg.junctions {
            if snap_position(&mut junc.position) {
                count += 1;
            }
        }
        for label in &mut seg.labels {
            if snap_position(&mut label.position) {
                count += 1;
            }
        }
    }

    for sheet_ref in &mut schematic.sheet_refs {
        if snap_position(&mut sheet_ref.position) {
            count += 1;
        }
        for pin in &mut sheet_ref.pins {
            if snap_scalar_to_grid(&mut pin.offset) {
                count += 1;
            }
        }
    }

    for port in &mut schematic.hierarchical_ports {
        if snap_position(&mut port.position) {
            count += 1;
        }
    }

    for power_port in &mut schematic.power_ports {
        if snap_position(&mut power_port.position) {
            count += 1;
        }
    }

    for power_flag in &mut schematic.power_flags {
        if snap_position(&mut power_flag.position) {
            count += 1;
        }
    }

    for bus in &mut schematic.bus_segments {
        for junction in &mut bus.junctions {
            if snap_position(&mut junction.position) {
                count += 1;
            }
        }
    }

    for entry in &mut schematic.bus_entries {
        if snap_position(&mut entry.position) {
            count += 1;
        }
    }

    count
}

/// Snap a position to the nearest grid point. Returns true if it moved.
fn snap_position(pos: &mut Position) -> bool {
    let sx = snap_to_grid(pos.x);
    let sy = snap_to_grid(pos.y);
    if !approx_eq(pos.x, sx) || !approx_eq(pos.y, sy) {
        pos.x = sx;
        pos.y = sy;
        return true;
    }
    false
}

fn snap_scalar_to_grid(value: &mut f64) -> bool {
    let snapped = snap_to_grid(*value);
    if !approx_eq(*value, snapped) {
        *value = snapped;
        return true;
    }
    false
}

// ===========================================================================
// 5. Remove orphan junctions
// ===========================================================================

/// Remove junctions that are not referenced by any line endpoint.
fn remove_orphan_junctions(schematic: &mut Schematic) -> usize {
    let mut total = 0;

    for seg in &mut schematic.net_segments {
        // Collect all junction UUIDs referenced by lines.
        let mut referenced: std::collections::HashSet<Uuid> = std::collections::HashSet::new();
        for line in &seg.lines {
            if let LineEndpoint::Junction { junction } = &line.from {
                referenced.insert(*junction);
            }
            if let LineEndpoint::Junction { junction } = &line.to {
                referenced.insert(*junction);
            }
        }

        let before = seg.junctions.len();
        seg.junctions.retain(|j| referenced.contains(&j.uuid));
        total += before - seg.junctions.len();
    }

    total
}

// ===========================================================================
// 6. Fix label-component overlaps
// ===========================================================================

/// Move labels that overlap with component bounding boxes above the component.
fn fix_label_component_overlaps(schematic: &mut Schematic, comp_boxes: &[BBox]) -> usize {
    let mut count = 0;

    for seg in &mut schematic.net_segments {
        for label in &mut seg.labels {
            for bbox in comp_boxes {
                if bbox.contains(label.position.x, label.position.y) {
                    // Move label above the component.
                    label.position.y = bbox.y0 - GRID;
                    count += 1;
                    break; // Fixed relative to first overlapping box.
                }
            }
        }
    }

    count
}

// ===========================================================================
// 7. Spread overlapping labels
// ===========================================================================

/// Nudge labels that are too close to each other (within 3×GRID distance).
fn spread_overlapping_labels(schematic: &mut Schematic) -> usize {
    // Collect (seg_index, label_index, position) for all labels.
    let mut label_refs: Vec<(usize, usize, f64, f64)> = Vec::new();
    for (si, seg) in schematic.net_segments.iter().enumerate() {
        for (li, label) in seg.labels.iter().enumerate() {
            label_refs.push((si, li, label.position.x, label.position.y));
        }
    }

    let threshold = 3.0 * GRID;
    let nudge_amount = 2.5 * GRID;
    let mut nudge_count = 0;

    // Compare every pair; nudge the second one found within threshold.
    for i in 0..label_refs.len() {
        for j in (i + 1)..label_refs.len() {
            let (si_j, li_j, _, _) = label_refs[j];
            let dx = label_refs[i].2 - label_refs[j].2;
            let dy = label_refs[i].3 - label_refs[j].3;
            let dist = (dx * dx + dy * dy).sqrt();

            if dist < threshold {
                // Nudge label j downward.
                let new_y = label_refs[j].3 + nudge_amount;
                schematic.net_segments[si_j].labels[li_j].position.y = new_y;
                label_refs[j].3 = new_y;
                nudge_count += 1;
            }
        }
    }

    nudge_count
}

// ===========================================================================
// 8. Compact layout
// ===========================================================================

/// Translate the entire schematic so the minimum coordinate is at (8×GRID, 8×GRID).
/// Returns true if a translation was applied.
fn compact_layout(schematic: &mut Schematic) -> bool {
    let mut min_x = f64::INFINITY;
    let mut min_y = f64::INFINITY;

    // Find minimum across symbols, junctions, labels.
    for sym in &schematic.symbols {
        min_x = min_x.min(sym.position.x);
        min_y = min_y.min(sym.position.y);
    }
    for seg in &schematic.net_segments {
        for junc in &seg.junctions {
            min_x = min_x.min(junc.position.x);
            min_y = min_y.min(junc.position.y);
        }
        for label in &seg.labels {
            min_x = min_x.min(label.position.x);
            min_y = min_y.min(label.position.y);
        }
    }

    for sheet_ref in &schematic.sheet_refs {
        min_x = min_x.min(sheet_ref.position.x);
        min_y = min_y.min(sheet_ref.position.y);
    }

    for port in &schematic.hierarchical_ports {
        min_x = min_x.min(port.position.x);
        min_y = min_y.min(port.position.y);
    }

    for power_port in &schematic.power_ports {
        min_x = min_x.min(power_port.position.x);
        min_y = min_y.min(power_port.position.y);
    }

    for power_flag in &schematic.power_flags {
        min_x = min_x.min(power_flag.position.x);
        min_y = min_y.min(power_flag.position.y);
    }

    for bus in &schematic.bus_segments {
        for junction in &bus.junctions {
            min_x = min_x.min(junction.position.x);
            min_y = min_y.min(junction.position.y);
        }
    }

    for entry in &schematic.bus_entries {
        min_x = min_x.min(entry.position.x);
        min_y = min_y.min(entry.position.y);
    }

    if min_x.is_infinite() || min_y.is_infinite() {
        return false;
    }

    let target = 8.0 * GRID;
    let dx = target - min_x;
    let dy = target - min_y;

    if approx_eq(dx, 0.0) && approx_eq(dy, 0.0) {
        return false;
    }

    // Translate symbol positions and their text positions.
    for sym in &mut schematic.symbols {
        sym.position.x += dx;
        sym.position.y += dy;
        for txt in &mut sym.texts {
            txt.position.x += dx;
            txt.position.y += dy;
        }
    }

    // Translate junctions, labels.
    for seg in &mut schematic.net_segments {
        for junc in &mut seg.junctions {
            junc.position.x += dx;
            junc.position.y += dy;
        }
        for label in &mut seg.labels {
            label.position.x += dx;
            label.position.y += dy;
        }
    }

    for sheet_ref in &mut schematic.sheet_refs {
        sheet_ref.position.x += dx;
        sheet_ref.position.y += dy;
    }

    for port in &mut schematic.hierarchical_ports {
        port.position.x += dx;
        port.position.y += dy;
    }

    for power_port in &mut schematic.power_ports {
        power_port.position.x += dx;
        power_port.position.y += dy;
    }

    for power_flag in &mut schematic.power_flags {
        power_flag.position.x += dx;
        power_flag.position.y += dy;
    }

    for bus in &mut schematic.bus_segments {
        for junction in &mut bus.junctions {
            junction.position.x += dx;
            junction.position.y += dy;
        }
    }

    for entry in &mut schematic.bus_entries {
        entry.position.x += dx;
        entry.position.y += dy;
    }

    true
}

// ===========================================================================
// Helper: compute component bounding boxes
// ===========================================================================

/// Compute axis-aligned bounding boxes for every placed component symbol.
///
/// Lookup chain per symbol:
/// `sym.component` → `circuit.components` → `lib_component` → `lib_comps`
/// → variant matching `lib_variant` → gate matching `sym.lib_gate` (or first)
/// → `gate.symbol` → `lib_syms`.
///
/// Pin positions and polygon vertices are rotated by the symbol's rotation,
/// then the bbox is padded by 1 mm on each side.
fn compute_comp_boxes(
    schematic: &Schematic,
    circuit: &Circuit,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &HashMap<Uuid, Symbol>,
) -> Vec<BBox> {
    let comp_by_uuid: HashMap<Uuid, &ComponentInstance> =
        circuit.components.iter().map(|c| (c.uuid, c)).collect();

    let mut boxes = Vec::new();

    for sym in &schematic.symbols {
        let Some(inst) = comp_by_uuid.get(&sym.component) else {
            continue;
        };
        let Some(lc) = lib_comps.get(&inst.lib_component) else {
            continue;
        };
        let Some(v) = lc.variants.iter().find(|v| v.uuid == inst.lib_variant) else {
            continue;
        };
        let Some(g) = v
            .gates
            .iter()
            .find(|g| g.uuid == sym.lib_gate)
            .or(v.gates.first())
        else {
            continue;
        };
        let Some(ls) = lib_syms.get(&g.symbol) else {
            continue;
        };

        let rot = sym.rotation.0.to_radians();
        let cos_r = rot.cos();
        let sin_r = rot.sin();
        let mut xs = Vec::new();
        let mut ys = Vec::new();

        for pin in &ls.pins {
            let wx = sym.position.x + pin.position.x * cos_r - pin.position.y * sin_r;
            let wy = sym.position.y + pin.position.x * sin_r + pin.position.y * cos_r;
            xs.push(wx);
            ys.push(wy);
        }
        for poly in &ls.polygons {
            for vert in &poly.vertices {
                let wx = sym.position.x + vert.position.x * cos_r - vert.position.y * sin_r;
                let wy = sym.position.y + vert.position.x * sin_r + vert.position.y * cos_r;
                xs.push(wx);
                ys.push(wy);
            }
        }

        if xs.is_empty() {
            continue;
        }

        let min_x = xs.iter().cloned().fold(f64::INFINITY, f64::min);
        let max_x = xs.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
        let min_y = ys.iter().cloned().fold(f64::INFINITY, f64::min);
        let max_y = ys.iter().cloned().fold(f64::NEG_INFINITY, f64::max);

        boxes.push(BBox::new(
            min_x - 1.0,
            min_y - 1.0,
            max_x + 1.0,
            max_y + 1.0,
        ));
    }

    boxes
}

// ===========================================================================
// Helper: remap endpoint
// ===========================================================================

/// If `ep` is a Junction endpoint whose UUID is in `remap`, replace it with the
/// mapped (canonical) UUID.
fn remap_endpoint(ep: &mut LineEndpoint, remap: &HashMap<Uuid, Uuid>) {
    if let LineEndpoint::Junction { junction } = ep {
        if let Some(&canonical) = remap.get(junction) {
            *junction = canonical;
        }
    }
}

// ===========================================================================
// Helper: endpoint position
// ===========================================================================

/// Resolve an endpoint to a world position.
/// - `Junction` endpoints are looked up in the provided junction slice.
/// - `Symbol` endpoints return `None` (pin world positions are not trivially
///   available in this context).
fn endpoint_position(ep: &LineEndpoint, junctions: &[Junction]) -> Option<(f64, f64)> {
    match ep {
        LineEndpoint::Junction { junction } => junction_position(*junction, junctions),
        LineEndpoint::Symbol { .. }
        | LineEndpoint::SheetPin { .. }
        | LineEndpoint::HierPort { .. } => None,
    }
}

// ===========================================================================
// Micro-helpers
// ===========================================================================

/// Round a position to an integer key at 0.001 mm resolution for dedup.
fn round_pos_key(x: f64, y: f64) -> (i64, i64) {
    ((x * 1000.0).round() as i64, (y * 1000.0).round() as i64)
}

/// Approximate equality within 0.0005 mm.
fn approx_eq(a: f64, b: f64) -> bool {
    (a - b).abs() < 0.0005
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_schematic() -> Schematic {
        Schematic {
            uuid: Uuid::new_v4(),
            name: "main".into(),
            grid: Grid {
                interval: GRID,
                unit: GridUnit::Millimeters,
            },
            symbols: vec![],
            net_segments: vec![],
            sheet_refs: vec![],
            hierarchical_ports: vec![],
            power_ports: vec![],
            power_flags: vec![],
            bus_segments: vec![],
            bus_entries: vec![],
            bus_aliases: vec![],
        }
    }

    #[test]
    fn snap_all_to_grid_moves_hierarchy_and_bus_primitives() {
        let mut schematic = make_schematic();
        schematic.sheet_refs.push(SheetRef {
            uuid: Uuid::new_v4(),
            name: "U_CHILD".into(),
            target_schematic: "child".into(),
            position: Position::new(2.7, 2.8),
            width: 20.0,
            height: 15.0,
            pins: vec![SheetRefPin {
                uuid: Uuid::new_v4(),
                name: "IO".into(),
                port_ref: Uuid::new_v4(),
                side: SheetSide::Left,
                offset: 2.7,
                net: None,
            }],
        });
        schematic.hierarchical_ports.push(HierarchicalPort {
            uuid: Uuid::new_v4(),
            name: "PORT".into(),
            position: Position::new(2.6, 2.5),
            side: SheetSide::Left,
            net: Uuid::new_v4(),
        });
        schematic.power_ports.push(PowerPort {
            uuid: Uuid::new_v4(),
            net: Uuid::new_v4(),
            position: Position::new(2.5, 2.6),
            rotation: Angle(0.0),
            style: "vcc".into(),
        });
        schematic.power_flags.push(PowerFlag {
            uuid: Uuid::new_v4(),
            net: Uuid::new_v4(),
            position: Position::new(2.6, 2.7),
        });
        schematic.bus_segments.push(BusSegment {
            uuid: Uuid::new_v4(),
            label: "D[0..3]".into(),
            junctions: vec![Junction {
                uuid: Uuid::new_v4(),
                position: Position::new(2.6, 5.3),
            }],
            lines: vec![],
        });
        schematic.bus_entries.push(BusEntry {
            uuid: Uuid::new_v4(),
            position: Position::new(2.7, 7.6),
            bus_segment: schematic.bus_segments[0].uuid,
            net: Uuid::new_v4(),
            member_name: "D[0]".into(),
        });

        let moved = snap_all_to_grid(&mut schematic);
        assert!(moved >= 6);
        assert_eq!(schematic.sheet_refs[0].position, Position::new(GRID, GRID));
        assert!((schematic.sheet_refs[0].pins[0].offset - GRID).abs() < 1e-6);
        assert_eq!(
            schematic.hierarchical_ports[0].position,
            Position::new(GRID, GRID)
        );
        assert_eq!(schematic.power_ports[0].position, Position::new(GRID, GRID));
        assert_eq!(schematic.power_flags[0].position, Position::new(GRID, GRID));
        assert_eq!(
            schematic.bus_segments[0].junctions[0].position,
            Position::new(GRID, 2.0 * GRID)
        );
        assert_eq!(
            schematic.bus_entries[0].position,
            Position::new(GRID, 3.0 * GRID)
        );
    }

    #[test]
    fn compact_layout_translates_hierarchy_and_bus_primitives() {
        let mut schematic = make_schematic();
        schematic.sheet_refs.push(SheetRef {
            uuid: Uuid::new_v4(),
            name: "U_CHILD".into(),
            target_schematic: "child".into(),
            position: Position::new(1.0, 2.0),
            width: 20.0,
            height: 15.0,
            pins: vec![],
        });
        schematic.hierarchical_ports.push(HierarchicalPort {
            uuid: Uuid::new_v4(),
            name: "PORT".into(),
            position: Position::new(2.0, 3.0),
            side: SheetSide::Right,
            net: Uuid::new_v4(),
        });
        schematic.power_ports.push(PowerPort {
            uuid: Uuid::new_v4(),
            net: Uuid::new_v4(),
            position: Position::new(3.0, 4.0),
            rotation: Angle(0.0),
            style: "vcc".into(),
        });
        schematic.power_flags.push(PowerFlag {
            uuid: Uuid::new_v4(),
            net: Uuid::new_v4(),
            position: Position::new(4.0, 5.0),
        });
        schematic.bus_segments.push(BusSegment {
            uuid: Uuid::new_v4(),
            label: "D[0..3]".into(),
            junctions: vec![Junction {
                uuid: Uuid::new_v4(),
                position: Position::new(5.0, 6.0),
            }],
            lines: vec![],
        });
        schematic.bus_entries.push(BusEntry {
            uuid: Uuid::new_v4(),
            position: Position::new(6.0, 7.0),
            bus_segment: schematic.bus_segments[0].uuid,
            net: Uuid::new_v4(),
            member_name: "D[0]".into(),
        });

        assert!(compact_layout(&mut schematic));
        assert_eq!(
            schematic.sheet_refs[0].position,
            Position::new(8.0 * GRID, 8.0 * GRID)
        );
        assert_eq!(
            schematic.hierarchical_ports[0].position,
            Position::new(21.32, 21.32)
        );
        assert_eq!(
            schematic.power_ports[0].position,
            Position::new(22.32, 22.32)
        );
        assert_eq!(
            schematic.power_flags[0].position,
            Position::new(23.32, 23.32)
        );
        assert_eq!(
            schematic.bus_segments[0].junctions[0].position,
            Position::new(24.32, 24.32)
        );
        assert_eq!(
            schematic.bus_entries[0].position,
            Position::new(25.32, 25.32)
        );
    }
}
