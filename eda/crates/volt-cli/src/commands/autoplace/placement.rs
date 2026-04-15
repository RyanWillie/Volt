//! Phase 4 (Orientation & Positioning) and Phase 5 (Companion Attachment)
//! for the Volt EDA autoplace algorithm.
//!
//! Phase 4 converts the ranked, ordered component lists into concrete world-space
//! positions (in grid units) with appropriate rotations. Phase 5 attaches companion
//! components (bypass caps, pull-ups, pull-downs) near their parent ICs and resolves
//! any bounding-box overlaps.

use std::collections::HashMap;
use uuid::Uuid;
use volt_core::library::{Component, Symbol};
use volt_core::project::*;
use super::types::*;

// Explicit import so autoplace `NetClass` (enum) wins over project `NetClass` (struct).
use super::types::NetClass;

// ===========================================================================
// Internal helpers
// ===========================================================================

/// Resolve the library [`Symbol`] for a [`ComponentInstance`] by walking:
/// instance → lib_component → variant (matching lib_variant) → first gate → symbol.
fn lookup_symbol_for_instance<'a>(
    inst: &ComponentInstance,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &'a HashMap<Uuid, Symbol>,
) -> Option<&'a Symbol> {
    let lib_comp = lib_comps.get(&inst.lib_component)?;
    let variant = lib_comp.variants.iter().find(|v| v.uuid == inst.lib_variant)?;
    let gate = variant.gates.first()?;
    lib_syms.get(&gate.symbol)
}

/// Compute the half-extents of a symbol from pin positions and polygon vertices.
/// Returns `(half_width, half_height)` in grid units, with a minimum of 2.0 each.
fn symbol_half_extents(sym: &Symbol) -> (f64, f64) {
    let mut max_x: f64 = 0.0;
    let mut max_y: f64 = 0.0;

    for pin in &sym.pins {
        max_x = max_x.max(pin.position.x.abs());
        max_y = max_y.max(pin.position.y.abs());
    }
    for poly in &sym.polygons {
        for v in &poly.vertices {
            max_x = max_x.max(v.position.x.abs());
            max_y = max_y.max(v.position.y.abs());
        }
    }

    let half_w = (max_x / GRID).max(2.0);
    let half_h = (max_y / GRID).max(2.0);
    (half_w, half_h)
}

/// Infer a [`ComponentRole`] from the flow graph, net classes, and pin profile.
///
/// This is used inside [`assign_coordinates`] which does not receive the
/// pre-computed role map.  The heuristic mirrors the full analysis classifier
/// closely enough for rotation decisions:
///
/// * **2-pin** — check net types to distinguish bypass / shunt / series passives.
/// * **Multi-pin** — check flow-graph connectivity for source / sink / processor.
fn infer_component_role(
    comp_uuid: &Uuid,
    circuit: &Circuit,
    net_classes: &HashMap<Uuid, NetClass>,
    flow_dag: &FlowGraph,
    pin_profile: &SymbolPinProfile,
) -> ComponentRole {
    let total_pins = pin_profile.left.len()
        + pin_profile.right.len()
        + pin_profile.top.len()
        + pin_profile.bottom.len();

    if total_pins <= 2 {
        // Classify 2-pin passive by counting power vs signal net connections.
        if let Some(inst) = circuit.components.iter().find(|c| c.uuid == *comp_uuid) {
            let mut power_count = 0usize;
            let mut signal_count = 0usize;
            for conn in &inst.signal_connections {
                if let Some(net_uuid) = conn.net {
                    match net_classes.get(&net_uuid) {
                        Some(NetClass::Power) => power_count += 1,
                        _ => signal_count += 1,
                    }
                }
            }
            if power_count >= 2 {
                return ComponentRole::BypassPassive;
            }
            if power_count >= 1 && signal_count >= 1 {
                return ComponentRole::ShuntPassive;
            }
        }
        return ComponentRole::SeriesPassive;
    }

    // Multi-pin: derive from flow-graph connectivity.
    let has_successors = !flow_dag.successors(comp_uuid).is_empty();
    let has_predecessors = !flow_dag.predecessors(comp_uuid).is_empty();

    match (has_predecessors, has_successors) {
        (false, true) => ComponentRole::Source,
        (true, false) => ComponentRole::Sink,
        _ => ComponentRole::Processor,
    }
}

/// Max half-width among a set of components. Returns 0.0 for an empty slice.
fn max_half_width(
    uuids: &[Uuid],
    sym_extents: &HashMap<Uuid, (f64, f64)>,
) -> f64 {
    let default = (2.0, 2.0);
    uuids
        .iter()
        .map(|u| sym_extents.get(u).unwrap_or(&default).0)
        .fold(0.0_f64, f64::max)
}

// ===========================================================================
// Phase 4 — Extent computation
// ===========================================================================

/// For each component instance, compute symbol half-extents `(half_width, half_height)`
/// in grid units.
///
/// Walks: component instance → lib_component → variant → first gate → symbol.
/// Measures max absolute pin and polygon-vertex positions, divides by [`GRID`],
/// and clamps to a minimum of `(2.0, 2.0)`.
pub fn compute_all_extents(
    circuit: &Circuit,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &HashMap<Uuid, Symbol>,
) -> HashMap<Uuid, (f64, f64)> {
    let mut extents = HashMap::with_capacity(circuit.components.len());
    for inst in &circuit.components {
        let (hw, hh) = match lookup_symbol_for_instance(inst, lib_comps, lib_syms) {
            Some(sym) => symbol_half_extents(sym),
            None => (2.0, 2.0),
        };
        extents.insert(inst.uuid, (hw, hh));
    }
    extents
}

// ===========================================================================
// Phase 4 — Pin profile computation
// ===========================================================================

/// For each component instance, look up the symbol and classify every pin by
/// the side it extends from, returning a [`SymbolPinProfile`].
pub fn compute_all_pin_profiles(
    circuit: &Circuit,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &HashMap<Uuid, Symbol>,
) -> HashMap<Uuid, SymbolPinProfile> {
    let mut profiles = HashMap::with_capacity(circuit.components.len());
    for inst in &circuit.components {
        let mut profile = SymbolPinProfile::default();
        if let Some(sym) = lookup_symbol_for_instance(inst, lib_comps, lib_syms) {
            for pin in &sym.pins {
                match classify_pin_side(pin.rotation.0) {
                    PinSide::Left => profile.left.push(pin.uuid),
                    PinSide::Right => profile.right.push(pin.uuid),
                    PinSide::Top => profile.top.push(pin.uuid),
                    PinSide::Bottom => profile.bottom.push(pin.uuid),
                }
            }
        }
        profiles.insert(inst.uuid, profile);
    }
    profiles
}

// ===========================================================================
// Phase 4 — Coordinate assignment
// ===========================================================================

/// Convert the ranked, ordered component lists into world-space grid positions.
///
/// **X (columns):**  Each column is separated by `base_gap` (6 grid units) plus the
/// max half-widths of the adjacent columns.  The first column starts at x = 0.
///
/// **Y (rows within a column):**  Components are stacked vertically with a 3 grid-unit
/// gap.  The first component in each column starts at y = 0.
///
/// After initial placement the column with the greatest total height is identified and
/// all other columns are centred vertically to align with its midpoint.
///
/// All final coordinates are snapped to the nearest integer grid unit.
pub fn assign_coordinates(
    rank_order: &[Vec<Uuid>],
    sym_extents: &HashMap<Uuid, (f64, f64)>,
    flow_dag: &FlowGraph,
    circuit: &Circuit,
    _lib_comps: &HashMap<Uuid, Component>,
    _lib_syms: &HashMap<Uuid, Symbol>,
    _net_members: &HashMap<Uuid, Vec<Uuid>>,
    net_classes: &HashMap<Uuid, NetClass>,
    pin_profiles: &HashMap<Uuid, SymbolPinProfile>,
) -> HashMap<Uuid, Placement> {
    if rank_order.is_empty() {
        return HashMap::new();
    }

    let default_extent = (2.0, 2.0);
    let col_gap: f64 = 6.0;
    let row_gap: f64 = 5.0;

    let mut placements: HashMap<Uuid, Placement> = HashMap::new();

    // Per-column bookkeeping for the vertical-centering pass.
    let mut column_total_heights: Vec<f64> = Vec::with_capacity(rank_order.len());
    let mut column_center_ys: Vec<f64> = Vec::with_capacity(rank_order.len());
    let mut column_x: f64 = 0.0;

    // ------------------------------------------------------------------
    // Pass 1: Assign initial (gx, gy) and rotation for every component.
    // ------------------------------------------------------------------
    for (col_idx, column) in rank_order.iter().enumerate() {
        if column.is_empty() {
            column_total_heights.push(0.0);
            column_center_ys.push(0.0);
            continue;
        }

        // Column X position.
        let cur_max_hw = max_half_width(column, sym_extents);
        if col_idx == 0 {
            column_x = 0.0;
        } else {
            let prev_max_hw = max_half_width(&rank_order[col_idx - 1], sym_extents);
            column_x += prev_max_hw + col_gap + cur_max_hw;
        }

        // Stack vertically within the column.
        let mut y: f64 = 0.0;
        for (row_idx, &comp_uuid) in column.iter().enumerate() {
            // Compute rotation first so we can swap extents if needed.
            let profile = pin_profiles
                .get(&comp_uuid)
                .cloned()
                .unwrap_or_default();
            let role = infer_component_role(
                &comp_uuid, circuit, net_classes, flow_dag, &profile,
            );
            let rotation = choose_rotation(comp_uuid, role, &profile, flow_dag, &placements);

            // Swap half-extents when component is rotated 90°/270°.
            let (raw_hw, raw_hh) = *sym_extents.get(&comp_uuid).unwrap_or(&default_extent);
            let (_hw, hh) = if rotation == 90.0 || rotation == 270.0 {
                (raw_hh, raw_hw)
            } else {
                (raw_hw, raw_hh)
            };

            if row_idx > 0 {
                let prev_uuid = column[row_idx - 1];
                let prev_rotation = placements.get(&prev_uuid).map(|p| p.rotation).unwrap_or(0.0);
                let (prev_raw_hw, prev_raw_hh) = *sym_extents.get(&prev_uuid).unwrap_or(&default_extent);
                let (_, prev_hh) = if prev_rotation == 90.0 || prev_rotation == 270.0 {
                    (prev_raw_hh, prev_raw_hw)
                } else {
                    (prev_raw_hw, prev_raw_hh)
                };
                y += prev_hh + row_gap + hh;
            }

            placements.insert(comp_uuid, Placement::new(column_x, y, rotation));
        }

        // Record column metrics for centering.
        let first_uuid = column[0];
        let last_uuid = *column.last().unwrap();
        let first_hh = sym_extents.get(&first_uuid).unwrap_or(&default_extent).1;
        let last_y = placements[&last_uuid].gy;
        let last_hh = sym_extents.get(&last_uuid).unwrap_or(&default_extent).1;

        let top_edge = 0.0 - first_hh; // first component y is 0
        let bottom_edge = last_y + last_hh;
        let total_height = bottom_edge - top_edge;
        let center_y = (top_edge + bottom_edge) / 2.0;

        column_total_heights.push(total_height);
        column_center_ys.push(center_y);
    }

    // ------------------------------------------------------------------
    // Pass 2: Vertical centering — align all columns to the tallest one.
    // ------------------------------------------------------------------
    if let Some((widest_idx, _)) = column_total_heights
        .iter()
        .enumerate()
        .max_by(|(_, a), (_, b)| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal))
    {
        let target_center = column_center_ys[widest_idx];
        for (col_idx, column) in rank_order.iter().enumerate() {
            if column.is_empty() || col_idx == widest_idx {
                continue;
            }
            let dy = target_center - column_center_ys[col_idx];
            for &comp_uuid in column {
                if let Some(pl) = placements.get_mut(&comp_uuid) {
                    pl.gy += dy;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Pass 3: Snap everything to the integer grid.
    // ------------------------------------------------------------------
    for pl in placements.values_mut() {
        pl.gx = snap_grid_units(pl.gx);
        pl.gy = snap_grid_units(pl.gy);
    }

    placements
}

// ===========================================================================
// Rotation selection
// ===========================================================================

/// Choose the best rotation (in degrees) for a component.
///
/// **2-pin components** (total pin count ≤ 2):
/// * [`SeriesPassive`](ComponentRole::SeriesPassive) → `0.0°` (horizontal)
/// * [`ShuntPassive`](ComponentRole::ShuntPassive) → `90.0°` (vertical)
/// * [`BypassPassive`](ComponentRole::BypassPassive) → `0.0°` (horizontal)
///
/// **Multi-pin components:**
/// * [`Processor`](ComponentRole::Processor) / [`Source`](ComponentRole::Source) /
///   [`Sink`](ComponentRole::Sink) → `0.0°` (standard, inputs left / outputs right)
/// * If every successor in the flow graph is to the **left** (lower X) of the
///   current placement → `180.0°` (flip so outputs face left)
/// * Default → `0.0°`
pub fn choose_rotation(
    comp_uuid: Uuid,
    comp_role: ComponentRole,
    pin_profile: &SymbolPinProfile,
    flow_dag: &FlowGraph,
    current_placements: &HashMap<Uuid, Placement>,
) -> f64 {
    let total_pins = pin_profile.left.len()
        + pin_profile.right.len()
        + pin_profile.top.len()
        + pin_profile.bottom.len();

    // --- 2-pin components ---
    if total_pins <= 2 {
        return match comp_role {
            ComponentRole::ShuntPassive => 90.0,
            // SeriesPassive, BypassPassive, and everything else → horizontal
            _ => 0.0,
        };
    }

    // --- Multi-pin: standard roles always get 0° ---
    match comp_role {
        ComponentRole::Processor | ComponentRole::Source | ComponentRole::Sink => {
            return 0.0;
        }
        _ => {}
    }

    // --- Multi-pin fallback: flip 180° if all successors are to the left ---
    if let Some(my_pl) = current_placements.get(&comp_uuid) {
        let successors = flow_dag.successors(&comp_uuid);
        if !successors.is_empty() {
            let all_left = successors.iter().all(|(succ_uuid, _)| {
                current_placements
                    .get(succ_uuid)
                    .map(|sp| sp.gx < my_pl.gx)
                    .unwrap_or(false)
            });
            if all_left {
                return 180.0;
            }
        }
    }

    0.0
}

// ===========================================================================
// Phase 5 — Companion attachment
// ===========================================================================

/// Place companion components (bypass caps, pull-ups, pull-downs) relative to
/// their parent IC.
///
/// * **Bypass** — below the parent: `(parent_gx, parent_gy + half_h + 3)`, rotation 0°
/// * **PullUp** — above the parent: `(parent_gx, parent_gy - half_h - 3)`, rotation 90°
/// * **PullDown** — below the parent: `(parent_gx, parent_gy + half_h + 3)`, rotation 90°
///
/// When multiple companions share the same parent each successive companion is
/// offset an additional 3 grid units in Y.  All positions are snapped to the
/// integer grid.
pub fn place_companions(
    companions: &[Companion],
    placements: &HashMap<Uuid, Placement>,
    sym_extents: &HashMap<Uuid, (f64, f64)>,
    _circuit: &Circuit,
    _lib_comps: &HashMap<Uuid, Component>,
    _lib_syms: &HashMap<Uuid, Symbol>,
) -> HashMap<Uuid, Placement> {
    let default_extent = (2.0, 2.0);
    let mut result: HashMap<Uuid, Placement> = HashMap::with_capacity(companions.len());

    // Track per-parent companion count for stacking offsets.
    let mut parent_count: HashMap<Uuid, usize> = HashMap::new();

    for companion in companions {
        let Some(parent_pl) = placements.get(&companion.parent) else {
            continue;
        };
        let (_, parent_hh) = *sym_extents
            .get(&companion.parent)
            .unwrap_or(&default_extent);

        let idx = *parent_count.get(&companion.parent).unwrap_or(&0);
        let stack_offset = idx as f64 * 3.0;

        let (gx, gy, rotation) = match companion.attachment {
            AttachmentType::Bypass => (
                parent_pl.gx,
                parent_pl.gy + parent_hh + 3.0 + stack_offset,
                0.0,
            ),
            AttachmentType::PullUp => (
                parent_pl.gx,
                parent_pl.gy - parent_hh - 3.0 - stack_offset,
                90.0,
            ),
            AttachmentType::PullDown => (
                parent_pl.gx,
                parent_pl.gy + parent_hh + 3.0 + stack_offset,
                90.0,
            ),
        };

        result.insert(
            companion.component,
            Placement::new(snap_grid_units(gx), snap_grid_units(gy), rotation),
        );

        *parent_count.entry(companion.parent).or_insert(0) += 1;
    }

    result
}

// ===========================================================================
// Overlap resolution
// ===========================================================================

/// Iteratively push overlapping components apart.
///
/// For every pair whose bounding boxes (position ± half-extent + 1 grid-unit
/// padding) overlap, the pair is pushed apart by half the overlap distance along
/// the axis of least penetration.
///
/// Runs for up to 20 iterations (or until no overlaps remain).  Coordinates are
/// re-snapped to the integer grid after each iteration.
pub fn resolve_overlaps(
    placements: &mut HashMap<Uuid, Placement>,
    sym_extents: &HashMap<Uuid, (f64, f64)>,
) {
    let default_extent = (2.0, 2.0);
    let padding: f64 = 2.0;

    let uuids: Vec<Uuid> = placements.keys().copied().collect();
    if uuids.len() < 2 {
        return;
    }

    for _iter in 0..20 {
        let mut any_overlap = false;

        for i in 0..uuids.len() {
            for j in (i + 1)..uuids.len() {
                let ua = uuids[i];
                let ub = uuids[j];

                let pa = *placements.get(&ua).unwrap();
                let pb = *placements.get(&ub).unwrap();

                let (hwa, hha) = *sym_extents.get(&ua).unwrap_or(&default_extent);
                let (hwb, hhb) = *sym_extents.get(&ub).unwrap_or(&default_extent);

                // Axis-aligned bounding boxes with padding.
                let a_left = pa.gx - hwa - padding;
                let a_right = pa.gx + hwa + padding;
                let a_top = pa.gy - hha - padding;
                let a_bottom = pa.gy + hha + padding;

                let b_left = pb.gx - hwb - padding;
                let b_right = pb.gx + hwb + padding;
                let b_top = pb.gy - hhb - padding;
                let b_bottom = pb.gy + hhb + padding;

                // No overlap?
                if a_left >= b_right || a_right <= b_left
                    || a_top >= b_bottom || a_bottom <= b_top
                {
                    continue;
                }

                any_overlap = true;

                let overlap_x = a_right.min(b_right) - a_left.max(b_left);
                let overlap_y = a_bottom.min(b_bottom) - a_top.max(b_top);

                // Push apart along the axis with least penetration.
                if overlap_x <= overlap_y {
                    let push = overlap_x / 2.0;
                    if pa.gx <= pb.gx {
                        placements.get_mut(&ua).unwrap().gx -= push;
                        placements.get_mut(&ub).unwrap().gx += push;
                    } else {
                        placements.get_mut(&ua).unwrap().gx += push;
                        placements.get_mut(&ub).unwrap().gx -= push;
                    }
                } else {
                    let push = overlap_y / 2.0;
                    if pa.gy <= pb.gy {
                        placements.get_mut(&ua).unwrap().gy -= push;
                        placements.get_mut(&ub).unwrap().gy += push;
                    } else {
                        placements.get_mut(&ua).unwrap().gy += push;
                        placements.get_mut(&ub).unwrap().gy -= push;
                    }
                }
            }
        }

        // Re-snap after each iteration.
        for pl in placements.values_mut() {
            pl.gx = snap_grid_units(pl.gx);
            pl.gy = snap_grid_units(pl.gy);
        }

        if !any_overlap {
            break;
        }
    }
}
