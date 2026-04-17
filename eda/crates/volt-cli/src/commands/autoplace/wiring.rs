//! Phase 6: Wiring & Label Placement
//!
//! Routes nets as Manhattan wires between component pins and places net labels.
//!
//! - **Power / HighFanout** nets get labels only (no wires) — one label per pin,
//!   offset upward for VCC-family or downward for GND-family.
//! - **Signal** nets get Manhattan-routed wires with junctions at bends, plus a
//!   single net label placed at the midpoint of all connected pins.
//!
//! All wire segments are checked against component bounding boxes after initial
//! routing; segments that pass through a body are rerouted around it.

use std::collections::HashMap;
use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Component, Symbol};
use volt_core::project::{
    Circuit, Junction, LineEndpoint, Net, NetLabel, SchematicLine, SchematicNetSegment,
    SchematicSymbol,
};

use super::types::{BBox, GRID, NetClass, is_ground_net_name, snap_to_grid};

/// Standard wire width in mm (matches LibrePCB default).
const WIRE_WIDTH: f64 = 0.15875;

/// Clearance to keep between wire and component body edge (mm).
const WIRE_CLEARANCE: f64 = 2.0;

// ===========================================================================
// Public API
// ===========================================================================

/// Route a single net and place its labels, returning a [`SchematicNetSegment`].
pub fn route_net(
    circuit: &Circuit,
    net: &Net,
    net_class: NetClass,
    sym_by_comp: &HashMap<Uuid, &SchematicSymbol>,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &HashMap<Uuid, Symbol>,
    comp_boxes: &[BBox],
) -> Option<SchematicNetSegment> {
    let endpoints = collect_pin_endpoints(circuit, net, sym_by_comp, lib_comps, lib_syms);
    if endpoints.is_empty() {
        return None;
    }

    match net_class {
        NetClass::Power | NetClass::HighFanout => route_power_net(net, &endpoints),
        NetClass::Signal => route_signal_net(net, &endpoints, comp_boxes),
    }
}

/// Collect the world-space pin positions for every component pin connected to `net`.
pub fn collect_pin_endpoints(
    circuit: &Circuit,
    net: &Net,
    sym_by_comp: &HashMap<Uuid, &SchematicSymbol>,
    lib_comps: &HashMap<Uuid, Component>,
    lib_syms: &HashMap<Uuid, Symbol>,
) -> Vec<(Uuid, Uuid, Position)> {
    let mut result = Vec::new();
    for inst in &circuit.components {
        for sc in &inst.signal_connections {
            if sc.net != Some(net.uuid) {
                continue;
            }
            let Some(sym) = sym_by_comp.get(&inst.uuid) else {
                continue;
            };
            let Some(lib_comp) = lib_comps.get(&inst.lib_component) else {
                continue;
            };
            let Some(variant) = lib_comp
                .variants
                .iter()
                .find(|v| v.uuid == inst.lib_variant)
            else {
                continue;
            };
            let gate = variant
                .gates
                .iter()
                .find(|g| g.uuid == sym.lib_gate)
                .or_else(|| variant.gates.first());
            let Some(gate) = gate else { continue };
            let Some(lib_sym) = lib_syms.get(&gate.symbol) else {
                continue;
            };
            for pm in &gate.pin_mappings {
                if pm.signal != sc.signal {
                    continue;
                }
                let Some(pin) = lib_sym.pins.iter().find(|p| p.uuid == pm.pin) else {
                    continue;
                };
                let rot = sym.rotation.0.to_radians();
                let wx = sym.position.x + pin.position.x * rot.cos() - pin.position.y * rot.sin();
                let wy = sym.position.y + pin.position.x * rot.sin() + pin.position.y * rot.cos();
                result.push((sym.uuid, pin.uuid, Position::new(wx, wy)));
            }
        }
    }
    result
}

// ===========================================================================
// Power / HighFanout routing — labels only
// ===========================================================================

fn route_power_net(net: &Net, endpoints: &[(Uuid, Uuid, Position)]) -> Option<SchematicNetSegment> {
    let is_ground = is_ground_net_name(&net.name);
    let y_offset = if is_ground { 2.0 * GRID } else { -2.0 * GRID };
    let x_offset = -2.0 * GRID;

    let labels: Vec<NetLabel> = endpoints
        .iter()
        .map(|(_sym, _pin, pos)| NetLabel {
            uuid: new_uuid(),
            position: Position::new(
                snap_to_grid(pos.x + x_offset),
                snap_to_grid(pos.y + y_offset),
            ),
            rotation: Angle(0.0),
            mirror: false,
        })
        .collect();

    Some(SchematicNetSegment {
        uuid: new_uuid(),
        net: net.uuid,
        junctions: Vec::new(),
        lines: Vec::new(),
        labels,
    })
}

// ===========================================================================
// Signal routing — wires + one label
// ===========================================================================

fn route_signal_net(
    net: &Net,
    endpoints: &[(Uuid, Uuid, Position)],
    comp_boxes: &[BBox],
) -> Option<SchematicNetSegment> {
    if endpoints.is_empty() {
        return None;
    }
    if endpoints.len() == 1 {
        let pos = endpoints[0].2;
        return Some(SchematicNetSegment {
            uuid: new_uuid(),
            net: net.uuid,
            junctions: Vec::new(),
            lines: Vec::new(),
            labels: vec![NetLabel {
                uuid: new_uuid(),
                position: Position::new(snap_to_grid(pos.x), snap_to_grid(pos.y - 3.0 * GRID)),
                rotation: Angle(0.0),
                mirror: false,
            }],
        });
    }

    let mut junctions: Vec<Junction> = Vec::new();
    let mut lines: Vec<SchematicLine> = Vec::new();

    if endpoints.len() == 2 {
        route_pair(endpoints, &mut junctions, &mut lines, comp_boxes);
    } else {
        route_multi(endpoints, &mut junctions, &mut lines, comp_boxes);
    }

    let label = place_signal_label(endpoints, comp_boxes);

    Some(SchematicNetSegment {
        uuid: new_uuid(),
        net: net.uuid,
        junctions,
        lines,
        labels: vec![label],
    })
}

// ===========================================================================
// Obstacle-aware pair routing (2 endpoints)
// ===========================================================================

/// Route between exactly two pin endpoints, detouring around component bodies.
fn route_pair(
    endpoints: &[(Uuid, Uuid, Position)],
    junctions: &mut Vec<Junction>,
    lines: &mut Vec<SchematicLine>,
    comp_boxes: &[BBox],
) {
    let (sym_a, pin_a, pos_a) = endpoints[0];
    let (sym_b, pin_b, pos_b) = endpoints[1];

    let from_ep = LineEndpoint::Symbol {
        symbol: sym_a,
        pin: pin_a,
    };
    let to_ep = LineEndpoint::Symbol {
        symbol: sym_b,
        pin: pin_b,
    };

    let dx = (pos_b.x - pos_a.x).abs();
    let dy = (pos_b.y - pos_a.y).abs();

    // Collinear — single straight wire, check for obstacles.
    if dx < 0.01 || dy < 0.01 {
        if !segment_hits_obstacle(pos_a, pos_b, comp_boxes, &[sym_a, sym_b]) {
            lines.push(SchematicLine {
                uuid: new_uuid(),
                width: WIRE_WIDTH,
                from: from_ep,
                to: to_ep,
            });
            return;
        }
        // Collinear but blocked — add a detour bump.
        let detour_x = find_clear_x_for_segment(pos_a, pos_b, comp_boxes, &[sym_a, sym_b]);
        let j1_uuid = new_uuid();
        let j2_uuid = new_uuid();
        let j1_pos = Position::new(snap_to_grid(detour_x), snap_to_grid(pos_a.y));
        let j2_pos = Position::new(snap_to_grid(detour_x), snap_to_grid(pos_b.y));
        junctions.push(Junction {
            uuid: j1_uuid,
            position: j1_pos,
        });
        junctions.push(Junction {
            uuid: j2_uuid,
            position: j2_pos,
        });
        lines.push(SchematicLine {
            uuid: new_uuid(),
            width: WIRE_WIDTH,
            from: from_ep,
            to: LineEndpoint::Junction { junction: j1_uuid },
        });
        lines.push(SchematicLine {
            uuid: new_uuid(),
            width: WIRE_WIDTH,
            from: LineEndpoint::Junction { junction: j1_uuid },
            to: LineEndpoint::Junction { junction: j2_uuid },
        });
        lines.push(SchematicLine {
            uuid: new_uuid(),
            width: WIRE_WIDTH,
            from: LineEndpoint::Junction { junction: j2_uuid },
            to: to_ep,
        });
        return;
    }

    // Non-collinear — try both bend directions, pick the one that avoids obstacles.
    let bend_hfirst = Position::new(snap_to_grid(pos_b.x), snap_to_grid(pos_a.y));
    let bend_vfirst = Position::new(snap_to_grid(pos_a.x), snap_to_grid(pos_b.y));

    let skip = &[sym_a, sym_b];
    let hfirst_clear = !segment_hits_obstacle(pos_a, bend_hfirst, comp_boxes, skip)
        && !segment_hits_obstacle(bend_hfirst, pos_b, comp_boxes, skip);
    let vfirst_clear = !segment_hits_obstacle(pos_a, bend_vfirst, comp_boxes, skip)
        && !segment_hits_obstacle(bend_vfirst, pos_b, comp_boxes, skip);

    if hfirst_clear || vfirst_clear {
        // Use the clear route (prefer horizontal-first when both work).
        let bend = if hfirst_clear {
            bend_hfirst
        } else {
            bend_vfirst
        };
        let junc_uuid = new_uuid();
        junctions.push(Junction {
            uuid: junc_uuid,
            position: bend,
        });
        lines.push(SchematicLine {
            uuid: new_uuid(),
            width: WIRE_WIDTH,
            from: from_ep,
            to: LineEndpoint::Junction {
                junction: junc_uuid,
            },
        });
        lines.push(SchematicLine {
            uuid: new_uuid(),
            width: WIRE_WIDTH,
            from: LineEndpoint::Junction {
                junction: junc_uuid,
            },
            to: to_ep,
        });
        return;
    }

    // Both bends are blocked — detour around. Route: A → clear_x,A.y → clear_x,B.y → B
    let detour_x = find_clear_x_for_segment(pos_a, pos_b, comp_boxes, skip);
    let j1_uuid = new_uuid();
    let j2_uuid = new_uuid();
    let j1_pos = Position::new(snap_to_grid(detour_x), snap_to_grid(pos_a.y));
    let j2_pos = Position::new(snap_to_grid(detour_x), snap_to_grid(pos_b.y));
    junctions.push(Junction {
        uuid: j1_uuid,
        position: j1_pos,
    });
    junctions.push(Junction {
        uuid: j2_uuid,
        position: j2_pos,
    });
    lines.push(SchematicLine {
        uuid: new_uuid(),
        width: WIRE_WIDTH,
        from: from_ep,
        to: LineEndpoint::Junction { junction: j1_uuid },
    });
    lines.push(SchematicLine {
        uuid: new_uuid(),
        width: WIRE_WIDTH,
        from: LineEndpoint::Junction { junction: j1_uuid },
        to: LineEndpoint::Junction { junction: j2_uuid },
    });
    lines.push(SchematicLine {
        uuid: new_uuid(),
        width: WIRE_WIDTH,
        from: LineEndpoint::Junction { junction: j2_uuid },
        to: to_ep,
    });
}

// ===========================================================================
// Obstacle-aware multi-endpoint routing (3+)
// ===========================================================================

/// Bus-style routing for 3+ endpoints with obstacle-aware horizontal stubs.
///
/// Creates a vertical trunk at a clear X position. Each endpoint connects to
/// the trunk via a horizontal stub that detours around any component bodies.
fn route_multi(
    endpoints: &[(Uuid, Uuid, Position)],
    junctions: &mut Vec<Junction>,
    lines: &mut Vec<SchematicLine>,
    comp_boxes: &[BBox],
) {
    // Sort endpoints by Y.
    let mut sorted: Vec<usize> = (0..endpoints.len()).collect();
    sorted.sort_by(|&a, &b| {
        endpoints[a]
            .2
            .y
            .partial_cmp(&endpoints[b].2.y)
            .unwrap_or(std::cmp::Ordering::Equal)
    });

    let min_y = endpoints[sorted[0]].2.y;
    let max_y = endpoints[*sorted.last().unwrap()].2.y;

    // Collect all symbol UUIDs in this net (to exclude from obstacle checks).
    let net_syms: Vec<Uuid> = endpoints.iter().map(|e| e.0).collect();

    // Find trunk X that avoids all component bodies in the Y range.
    let trunk_x = find_clear_trunk_x(endpoints, comp_boxes, &net_syms, min_y, max_y);

    // Create trunk junctions and horizontal stubs.
    let mut trunk_junctions: Vec<(Uuid, Position)> = Vec::new();
    for &idx in &sorted {
        let (sym, pin, pos) = endpoints[idx];
        let trunk_pos = Position::new(snap_to_grid(trunk_x), snap_to_grid(pos.y));
        let junc_uuid = new_uuid();
        junctions.push(Junction {
            uuid: junc_uuid,
            position: trunk_pos,
        });
        trunk_junctions.push((junc_uuid, trunk_pos));

        let from_ep = LineEndpoint::Symbol { symbol: sym, pin };
        let to_ep = LineEndpoint::Junction {
            junction: junc_uuid,
        };

        // Check if horizontal stub crosses an obstacle.
        if (pos.x - trunk_x).abs() < 0.01 {
            // Already at trunk X — direct zero-length or nearly-zero connection.
            lines.push(SchematicLine {
                uuid: new_uuid(),
                width: WIRE_WIDTH,
                from: from_ep,
                to: to_ep,
            });
        } else if !segment_hits_obstacle(pos, trunk_pos, comp_boxes, &net_syms) {
            // Clear path — direct horizontal stub.
            lines.push(SchematicLine {
                uuid: new_uuid(),
                width: WIRE_WIDTH,
                from: from_ep,
                to: to_ep,
            });
        } else {
            // Stub crosses a component — detour vertically to clear it.
            let clear_y = find_clear_y_for_hstub(pos, trunk_pos, comp_boxes, &net_syms);
            let j_mid = new_uuid();
            let j_mid_pos = Position::new(snap_to_grid(pos.x), snap_to_grid(clear_y));
            junctions.push(Junction {
                uuid: j_mid,
                position: j_mid_pos,
            });

            let j_corner = new_uuid();
            let j_corner_pos = Position::new(snap_to_grid(trunk_x), snap_to_grid(clear_y));
            junctions.push(Junction {
                uuid: j_corner,
                position: j_corner_pos,
            });

            // Pin → straight down/up → horizontal to trunk X → straight to trunk Y
            lines.push(SchematicLine {
                uuid: new_uuid(),
                width: WIRE_WIDTH,
                from: from_ep,
                to: LineEndpoint::Junction { junction: j_mid },
            });
            lines.push(SchematicLine {
                uuid: new_uuid(),
                width: WIRE_WIDTH,
                from: LineEndpoint::Junction { junction: j_mid },
                to: LineEndpoint::Junction { junction: j_corner },
            });
            lines.push(SchematicLine {
                uuid: new_uuid(),
                width: WIRE_WIDTH,
                from: LineEndpoint::Junction { junction: j_corner },
                to: to_ep,
            });
        }
    }

    // Vertical trunk segments between adjacent junctions.
    for pair in trunk_junctions.windows(2) {
        lines.push(SchematicLine {
            uuid: new_uuid(),
            width: WIRE_WIDTH,
            from: LineEndpoint::Junction {
                junction: pair[0].0,
            },
            to: LineEndpoint::Junction {
                junction: pair[1].0,
            },
        });
    }
}

// ===========================================================================
// Obstacle detection helpers
// ===========================================================================

/// Check if a wire segment from `a` to `b` passes through any component body.
///
/// Endpoints touching a component box are expected (pin starts at the body
/// edge), so we sample the MIDDLE of the segment rather than checking the
/// full extent.  For a segment longer than 2×GRID we check several interior
/// sample points against every component box.
fn segment_hits_obstacle(
    a: Position,
    b: Position,
    comp_boxes: &[BBox],
    _skip_syms: &[Uuid],
) -> bool {
    let length = ((b.x - a.x).powi(2) + (b.y - a.y).powi(2)).sqrt();
    if length < 0.01 {
        return false; // zero-length segment
    }

    // Number of interior sample points (skip first/last 20% to avoid
    // triggering on the endpoint components).
    let samples = ((length / GRID).ceil() as usize).max(3);
    for i in 1..samples {
        let t = 0.2 + 0.6 * (i as f64 / samples as f64);
        let px = a.x + (b.x - a.x) * t;
        let py = a.y + (b.y - a.y) * t;
        if comp_boxes.iter().any(|cb| cb.contains(px, py)) {
            return true;
        }
    }
    false
}

/// Compute axis-aligned bounding box of a wire segment.
fn segment_bbox(a: Position, b: Position) -> BBox {
    BBox::new(
        a.x.min(b.x) - 0.1,
        a.y.min(b.y) - 0.1,
        a.x.max(b.x) + 0.1,
        a.y.max(b.y) + 0.1,
    )
}

/// Shrink a segment bbox inward from each end to avoid triggering on the
/// endpoint components themselves.
fn shrink_segment_bbox(a: Position, b: Position, amount: f64) -> BBox {
    let min_x = a.x.min(b.x);
    let max_x = a.x.max(b.x);
    let min_y = a.y.min(b.y);
    let max_y = a.y.max(b.y);

    // For horizontal segments, shrink X ends. For vertical, shrink Y ends.
    let dx = max_x - min_x;
    let dy = max_y - min_y;

    if dx > dy {
        // Mostly horizontal.
        BBox::new(min_x + amount, min_y - 0.1, max_x - amount, max_y + 0.1)
    } else {
        // Mostly vertical.
        BBox::new(min_x - 0.1, min_y + amount, max_x + 0.1, max_y - amount)
    }
}

/// Find a clear X position for a vertical detour segment between two Y values.
fn find_clear_x_for_segment(
    a: Position,
    b: Position,
    comp_boxes: &[BBox],
    skip_syms: &[Uuid],
) -> f64 {
    let min_y = a.y.min(b.y);
    let max_y = a.y.max(b.y);
    let mid_x = (a.x + b.x) / 2.0;

    // Try offsets from the midpoint.
    for steps in 0..=20 {
        let offset = steps as f64 * GRID;
        for candidate in [mid_x + offset, mid_x - offset] {
            let test_box = BBox::new(candidate - 0.5, min_y, candidate + 0.5, max_y);
            if !comp_boxes.iter().any(|cb| test_box.overlaps(cb)) {
                return snap_to_grid(candidate);
            }
        }
    }
    snap_to_grid(mid_x + 15.0 * GRID)
}

/// Find a clear trunk X for multi-endpoint routing.
///
/// Tries positions to the right and left of the rightmost/leftmost endpoint,
/// checking that a vertical strip through the full Y range doesn't overlap
/// any component body (excluding components in the net).
fn find_clear_trunk_x(
    endpoints: &[(Uuid, Uuid, Position)],
    comp_boxes: &[BBox],
    _net_syms: &[Uuid],
    min_y: f64,
    max_y: f64,
) -> f64 {
    // Start from the rightmost endpoint X + clearance.
    let max_ep_x = endpoints
        .iter()
        .map(|e| e.2.x)
        .fold(f64::NEG_INFINITY, f64::max);
    let min_ep_x = endpoints
        .iter()
        .map(|e| e.2.x)
        .fold(f64::INFINITY, f64::min);

    let is_clear = |x: f64| -> bool {
        let trunk_box = BBox::new(x - 1.0, min_y - GRID, x + 1.0, max_y + GRID);
        !comp_boxes.iter().any(|cb| trunk_box.overlaps(cb))
    };

    // Try just right of the rightmost endpoint first.
    let right_start = max_ep_x + WIRE_CLEARANCE + GRID;
    if is_clear(right_start) {
        return snap_to_grid(right_start);
    }

    // Try just left of the leftmost endpoint.
    let left_start = min_ep_x - WIRE_CLEARANCE - GRID;
    if is_clear(left_start) {
        return snap_to_grid(left_start);
    }

    // Probe outward from the right.
    for steps in 1..=15 {
        let offset = steps as f64 * GRID;
        let right = max_ep_x + offset;
        if is_clear(right) {
            return snap_to_grid(right);
        }
        let left = min_ep_x - offset;
        if is_clear(left) {
            return snap_to_grid(left);
        }
    }

    snap_to_grid(max_ep_x + 15.0 * GRID)
}

/// Find a clear Y for a horizontal stub detour.
///
/// When a horizontal stub from a pin to the trunk crosses a component body,
/// we need to go vertically first to a Y that's above or below the obstacle,
/// then horizontally to the trunk.
fn find_clear_y_for_hstub(
    pin_pos: Position,
    trunk_pos: Position,
    comp_boxes: &[BBox],
    _net_syms: &[Uuid],
) -> f64 {
    let min_x = pin_pos.x.min(trunk_pos.x);
    let max_x = pin_pos.x.max(trunk_pos.x);

    // Find the blocking component(s).
    let stub_box = BBox::new(min_x, pin_pos.y - 0.5, max_x, pin_pos.y + 0.5);
    let blockers: Vec<&BBox> = comp_boxes
        .iter()
        .filter(|cb| stub_box.overlaps(cb))
        .collect();

    if blockers.is_empty() {
        return pin_pos.y; // shouldn't happen, but safe fallback
    }

    // Try going above the topmost blocker.
    let top_edge = blockers.iter().map(|b| b.y0).fold(f64::INFINITY, f64::min);
    let above = top_edge - WIRE_CLEARANCE;

    // Try going below the bottommost blocker.
    let bottom_edge = blockers
        .iter()
        .map(|b| b.y1)
        .fold(f64::NEG_INFINITY, f64::max);
    let below = bottom_edge + WIRE_CLEARANCE;

    // Pick whichever is closer to the pin's Y.
    if (above - pin_pos.y).abs() <= (below - pin_pos.y).abs() {
        snap_to_grid(above)
    } else {
        snap_to_grid(below)
    }
}

// ===========================================================================
// Label placement
// ===========================================================================

fn place_signal_label(endpoints: &[(Uuid, Uuid, Position)], comp_boxes: &[BBox]) -> NetLabel {
    let n = endpoints.len() as f64;
    let mid_x: f64 = endpoints.iter().map(|(_, _, p)| p.x).sum::<f64>() / n;
    let mid_y: f64 = endpoints.iter().map(|(_, _, p)| p.y).sum::<f64>() / n;

    let label_width = 4.0 * GRID;
    let label_height = 1.5 * GRID;
    let lx = snap_to_grid(mid_x - 3.0 * GRID);
    let ly = snap_to_grid(mid_y);

    let label_box = BBox::new(lx, ly - label_height, lx + label_width, ly);
    let overlaps = comp_boxes.iter().any(|b| label_box.overlaps(b));

    let (lx, ly) = if overlaps {
        (snap_to_grid(mid_x), snap_to_grid(mid_y - 4.0 * GRID))
    } else {
        (lx, ly)
    };

    NetLabel {
        uuid: new_uuid(),
        position: Position::new(lx, ly),
        rotation: Angle(0.0),
        mirror: false,
    }
}
