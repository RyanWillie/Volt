//! Board-level Design Rule Checking (DRC).
//!
//! Validates a [`Board`] layout against its [`DrcSettings`] and returns a list
//! of [`DrcDiagnostic`]s. Each diagnostic has a severity (Error, Warning) and
//! a human-readable message.
//!
//! # Rules implemented
//!
//! | ID   | Severity | Description |
//! |------|----------|-------------|
//! | D001 | Error    | Copper-to-copper clearance violation |
//! | D002 | Error    | Copper-to-board-edge clearance violation |
//! | D003 | Error    | Copper-to-NPTH clearance violation |
//! | D004 | Error    | Drill-to-drill clearance violation |
//! | D005 | Error    | Drill-to-board-edge clearance violation |
//! | D006 | Error    | Minimum trace width violation |
//! | D007 | Error    | Minimum annular ring violation |
//! | D008 | Error    | Minimum NPTH drill diameter |
//! | D009 | Error    | Minimum PTH drill diameter |
//! | D010 | Warning  | Unplaced device (position 0,0) |
//! | D011 | Error    | Missing board outline |
//! | D012 | Warning  | Overlapping devices |
//! | D013 | Error    | Blind/buried via used when not allowed |

use std::collections::HashMap;

use serde::{Deserialize, Serialize};
use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Device, FootprintPad, Package};
use volt_core::project::{Board, BoardDevice, Circuit, TraceEndpoint, Via, ViaSize};

// ===========================================================================
// Public types
// ===========================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Severity {
    Error,
    Warning,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DrcDiagnostic {
    /// Rule ID (e.g. "D001", "D010").
    pub rule: String,
    pub severity: Severity,
    pub message: String,
    /// World-space location of the violation, if applicable.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub location: Option<Position>,
    /// UUID of the related object, if applicable.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub object: Option<Uuid>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DrcResult {
    pub diagnostics: Vec<DrcDiagnostic>,
    pub errors: usize,
    pub warnings: usize,
    pub passed: bool,
}

// ===========================================================================
// Library resolver
// ===========================================================================

/// Provides access to library devices and packages for DRC checks that need
/// footprint metadata (pad sizes, holes, etc.).
pub trait BoardLibrary {
    fn get_device(&self, uuid: &Uuid) -> Option<&Device>;
    fn get_package(&self, uuid: &Uuid) -> Option<&Package>;
}

/// Simple map-based resolver.
pub struct MapBoardLibrary {
    pub devices: HashMap<Uuid, Device>,
    pub packages: HashMap<Uuid, Package>,
}

impl BoardLibrary for MapBoardLibrary {
    fn get_device(&self, uuid: &Uuid) -> Option<&Device> {
        self.devices.get(uuid)
    }

    fn get_package(&self, uuid: &Uuid) -> Option<&Package> {
        self.packages.get(uuid)
    }
}

// ===========================================================================
// Geometry helpers
// ===========================================================================

/// Transform a footprint-local point to world coordinates.
fn transform_point(px: f64, py: f64, bd: &BoardDevice) -> (f64, f64) {
    let mut lx = px;
    let ly = py;

    // Mirror X if device is flipped to bottom side
    if bd.flip {
        lx = -lx;
    }

    // Rotate by device rotation
    let theta = bd.rotation.0.to_radians();
    let cos_t = theta.cos();
    let sin_t = theta.sin();
    let rx = lx * cos_t - ly * sin_t;
    let ry = lx * sin_t + ly * cos_t;

    // Translate by device position
    (bd.position.x + rx, bd.position.y + ry)
}

/// Distance between two points.
fn point_distance(x1: f64, y1: f64, x2: f64, y2: f64) -> f64 {
    ((x2 - x1).powi(2) + (y2 - y1).powi(2)).sqrt()
}

/// Minimum distance from point (px, py) to the line segment (ax, ay)–(bx, by).
fn point_to_segment_distance(px: f64, py: f64, ax: f64, ay: f64, bx: f64, by: f64) -> f64 {
    let dx = bx - ax;
    let dy = by - ay;
    let len_sq = dx * dx + dy * dy;

    if len_sq < 1e-12 {
        // Degenerate segment (a == b)
        return point_distance(px, py, ax, ay);
    }

    // Project point onto line, clamped to [0, 1]
    let t = ((px - ax) * dx + (py - ay) * dy) / len_sq;
    let t = t.clamp(0.0, 1.0);

    let proj_x = ax + t * dx;
    let proj_y = ay + t * dy;

    point_distance(px, py, proj_x, proj_y)
}

/// Minimum distance between two line segments.
fn segment_to_segment_distance(
    a1x: f64, a1y: f64, a2x: f64, a2y: f64,
    b1x: f64, b1y: f64, b2x: f64, b2y: f64,
) -> f64 {
    // Check all four endpoint-to-segment distances + segment intersection
    let d1 = point_to_segment_distance(a1x, a1y, b1x, b1y, b2x, b2y);
    let d2 = point_to_segment_distance(a2x, a2y, b1x, b1y, b2x, b2y);
    let d3 = point_to_segment_distance(b1x, b1y, a1x, a1y, a2x, a2y);
    let d4 = point_to_segment_distance(b2x, b2y, a1x, a1y, a2x, a2y);

    let mut min_d = d1.min(d2).min(d3).min(d4);

    // Check if segments actually intersect
    if segments_intersect(a1x, a1y, a2x, a2y, b1x, b1y, b2x, b2y) {
        min_d = 0.0;
    }

    min_d
}

/// Check if two line segments intersect (proper or at endpoints).
fn segments_intersect(
    a1x: f64, a1y: f64, a2x: f64, a2y: f64,
    b1x: f64, b1y: f64, b2x: f64, b2y: f64,
) -> bool {
    let d1 = cross(b1x, b1y, b2x, b2y, a1x, a1y);
    let d2 = cross(b1x, b1y, b2x, b2y, a2x, a2y);
    let d3 = cross(a1x, a1y, a2x, a2y, b1x, b1y);
    let d4 = cross(a1x, a1y, a2x, a2y, b2x, b2y);

    if ((d1 > 0.0 && d2 < 0.0) || (d1 < 0.0 && d2 > 0.0))
        && ((d3 > 0.0 && d4 < 0.0) || (d3 < 0.0 && d4 > 0.0))
    {
        return true;
    }

    // Collinear cases
    if d1.abs() < 1e-12 && on_segment(b1x, b1y, b2x, b2y, a1x, a1y) { return true; }
    if d2.abs() < 1e-12 && on_segment(b1x, b1y, b2x, b2y, a2x, a2y) { return true; }
    if d3.abs() < 1e-12 && on_segment(a1x, a1y, a2x, a2y, b1x, b1y) { return true; }
    if d4.abs() < 1e-12 && on_segment(a1x, a1y, a2x, a2y, b2x, b2y) { return true; }

    false
}

fn cross(ox: f64, oy: f64, ax: f64, ay: f64, bx: f64, by: f64) -> f64 {
    (ax - ox) * (by - oy) - (ay - oy) * (bx - ox)
}

fn on_segment(ax: f64, ay: f64, bx: f64, by: f64, px: f64, py: f64) -> bool {
    px >= ax.min(bx) && px <= ax.max(bx) && py >= ay.min(by) && py <= ay.max(by)
}

/// Minimum distance from a point to any edge of a polygon (list of vertices).
fn point_to_polygon_edge_distance(px: f64, py: f64, vertices: &[(f64, f64)]) -> f64 {
    if vertices.is_empty() {
        return f64::MAX;
    }
    let n = vertices.len();
    let mut min_d = f64::MAX;
    for i in 0..n {
        let j = (i + 1) % n;
        let d = point_to_segment_distance(
            px, py,
            vertices[i].0, vertices[i].1,
            vertices[j].0, vertices[j].1,
        );
        if d < min_d {
            min_d = d;
        }
    }
    min_d
}

/// Minimum distance from a segment to any edge of a polygon.
fn segment_to_polygon_edge_distance(
    sx1: f64, sy1: f64, sx2: f64, sy2: f64,
    vertices: &[(f64, f64)],
) -> f64 {
    if vertices.is_empty() {
        return f64::MAX;
    }
    let n = vertices.len();
    let mut min_d = f64::MAX;
    for i in 0..n {
        let j = (i + 1) % n;
        let d = segment_to_segment_distance(
            sx1, sy1, sx2, sy2,
            vertices[i].0, vertices[i].1,
            vertices[j].0, vertices[j].1,
        );
        if d < min_d {
            min_d = d;
        }
    }
    min_d
}

// ===========================================================================
// Trace endpoint resolution
// ===========================================================================

/// Resolved world-space position of a trace endpoint.
fn resolve_endpoint(
    ep: &TraceEndpoint,
    board: &Board,
    library: &dyn BoardLibrary,
) -> Option<(f64, f64)> {
    match ep {
        TraceEndpoint::Junction { junction } => {
            for seg in &board.net_segments {
                for j in &seg.junctions {
                    if j.uuid == *junction {
                        return Some((j.position.x, j.position.y));
                    }
                }
            }
            None
        }
        TraceEndpoint::Via { via } => {
            for seg in &board.net_segments {
                for v in &seg.vias {
                    if v.uuid == *via {
                        return Some((v.position.x, v.position.y));
                    }
                }
            }
            None
        }
        TraceEndpoint::Device { device, pad } => {
            // Find the board device, then resolve pad from footprint
            let bd = board.devices.iter().find(|d| d.component == *device)?;
            let lib_dev = library.get_device(&bd.lib_device)?;
            let pkg = library.get_package(&lib_dev.package)?;
            let fp = pkg.footprints.iter().find(|f| f.uuid == bd.lib_footprint)?;
            let fp_pad = fp.pads.iter().find(|p| p.uuid == *pad)?;
            Some(transform_point(fp_pad.position.x, fp_pad.position.y, bd))
        }
    }
}

// ===========================================================================
// Copper object representation (for clearance checks)
// ===========================================================================

/// A copper object in world space for clearance checking.
#[derive(Debug, Clone)]
enum CopperObject {
    /// A trace segment on a copper layer.
    TraceSegment {
        uuid: Uuid,
        layer: Layer,
        width: f64,
        x1: f64,
        y1: f64,
        x2: f64,
        y2: f64,
    },
    /// A pad (from device or standalone) at a world position.
    Pad {
        uuid: Uuid,
        /// Layers this pad exists on (top, bottom, or both for THT).
        layers: Vec<Layer>,
        x: f64,
        y: f64,
        width: f64,
        height: f64,
    },
}

/// A drill hole in world space.
#[derive(Debug, Clone)]
struct DrillHole {
    uuid: Uuid,
    x: f64,
    y: f64,
    diameter: f64,
    plated: bool,
}

/// Collect all copper objects from the board.
fn collect_copper_objects(
    board: &Board,
    library: &dyn BoardLibrary,
) -> Vec<CopperObject> {
    let mut objects = Vec::new();

    // Traces
    for seg in &board.net_segments {
        for trace in &seg.traces {
            let from = resolve_endpoint(&trace.from, board, library);
            let to = resolve_endpoint(&trace.to, board, library);
            if let (Some((x1, y1)), Some((x2, y2))) = (from, to) {
                objects.push(CopperObject::TraceSegment {
                    uuid: trace.uuid,
                    layer: trace.layer,
                    width: trace.width,
                    x1, y1, x2, y2,
                });
            }
        }
    }

    // Device pads
    for bd in &board.devices {
        if let Some(lib_dev) = library.get_device(&bd.lib_device) {
            if let Some(pkg) = library.get_package(&lib_dev.package) {
                if let Some(fp) = pkg.footprints.iter().find(|f| f.uuid == bd.lib_footprint) {
                    for fp_pad in &fp.pads {
                        let (wx, wy) = transform_point(fp_pad.position.x, fp_pad.position.y, bd);
                        let layers = pad_copper_layers(fp_pad, bd);
                        objects.push(CopperObject::Pad {
                            uuid: fp_pad.uuid,
                            layers,
                            x: wx,
                            y: wy,
                            width: fp_pad.width,
                            height: fp_pad.height,
                        });
                    }
                }
            }
        }
    }

    // Via pads (treat as round pads on their copper layers)
    for seg in &board.net_segments {
        for via in &seg.vias {
            let outer = via_outer_diameter(via, board);
            let layers = via_copper_layers(via, board);
            objects.push(CopperObject::Pad {
                uuid: via.uuid,
                layers,
                x: via.position.x,
                y: via.position.y,
                width: outer,
                height: outer,
            });
        }
    }

    // Standalone board pads
    for seg in &board.net_segments {
        for bpad in &seg.pads {
            let layers = match bpad.side {
                PadSide::Top => vec![Layer::TopCopper],
                PadSide::Bottom => vec![Layer::BottomCopper],
                PadSide::ThroughHole => vec![Layer::TopCopper, Layer::BottomCopper],
            };
            objects.push(CopperObject::Pad {
                uuid: bpad.uuid,
                layers,
                x: bpad.position.x,
                y: bpad.position.y,
                width: bpad.size_width,
                height: bpad.size_height,
            });
        }
    }

    objects
}

/// Determine which copper layers a footprint pad exists on.
fn pad_copper_layers(fp_pad: &FootprintPad, bd: &BoardDevice) -> Vec<Layer> {
    match fp_pad.side {
        PadSide::ThroughHole => vec![Layer::TopCopper, Layer::BottomCopper],
        PadSide::Top => {
            if bd.flip {
                vec![Layer::BottomCopper]
            } else {
                vec![Layer::TopCopper]
            }
        }
        PadSide::Bottom => {
            if bd.flip {
                vec![Layer::TopCopper]
            } else {
                vec![Layer::BottomCopper]
            }
        }
    }
}

/// Via outer diameter from settings.
fn via_outer_diameter(via: &Via, board: &Board) -> f64 {
    match via.size {
        ViaSize::Manual(d) => d,
        ViaSize::Auto => {
            // Use annular ring ratio from design rules
            let ring = board.design_rules.via_annular_ring_ratio * via.drill;
            let ring = ring
                .max(board.design_rules.via_annular_ring_min)
                .min(board.design_rules.via_annular_ring_max);
            via.drill + 2.0 * ring
        }
    }
}

/// Via copper layers (from/to).
fn via_copper_layers(via: &Via, board: &Board) -> Vec<Layer> {
    let all_layers = copper_layer_stack(board);
    let from_idx = all_layers.iter().position(|l| *l == via.from_layer);
    let to_idx = all_layers.iter().position(|l| *l == via.to_layer);
    match (from_idx, to_idx) {
        (Some(a), Some(b)) => {
            let lo = a.min(b);
            let hi = a.max(b);
            all_layers[lo..=hi].to_vec()
        }
        _ => vec![via.from_layer, via.to_layer],
    }
}

/// Ordered copper layer stack for the board.
fn copper_layer_stack(board: &Board) -> Vec<Layer> {
    let mut layers = vec![Layer::TopCopper];
    for i in 1..=board.inner_layers {
        layers.push(Layer::InnerCopper(i));
    }
    layers.push(Layer::BottomCopper);
    layers
}

/// Check if two copper objects share any layer.
fn share_layer(a_layers: &[Layer], b_layers: &[Layer]) -> bool {
    a_layers.iter().any(|la| b_layers.iter().any(|lb| la == lb))
}

/// Collect all drill holes from the board.
fn collect_drill_holes(
    board: &Board,
    library: &dyn BoardLibrary,
) -> Vec<DrillHole> {
    let mut holes = Vec::new();

    // Vias (plated)
    for seg in &board.net_segments {
        for via in &seg.vias {
            holes.push(DrillHole {
                uuid: via.uuid,
                x: via.position.x,
                y: via.position.y,
                diameter: via.drill,
                plated: true,
            });
        }
    }

    // NPTH board holes
    for hole in &board.holes {
        // Use first vertex position as the hole center
        if let Some(v) = hole.path.first() {
            holes.push(DrillHole {
                uuid: hole.uuid,
                x: v.position.x,
                y: v.position.y,
                diameter: hole.diameter,
                plated: false,
            });
        }
    }

    // Device pad holes (plated THT)
    for bd in &board.devices {
        if let Some(lib_dev) = library.get_device(&bd.lib_device) {
            if let Some(pkg) = library.get_package(&lib_dev.package) {
                if let Some(fp) = pkg.footprints.iter().find(|f| f.uuid == bd.lib_footprint) {
                    for fp_pad in &fp.pads {
                        for hole in &fp_pad.holes {
                            let (wx, wy) = transform_point(
                                fp_pad.position.x, fp_pad.position.y, bd,
                            );
                            holes.push(DrillHole {
                                uuid: hole.uuid,
                                x: wx,
                                y: wy,
                                diameter: hole.diameter,
                                plated: true,
                            });
                        }
                    }
                }
            }
        }
    }

    // Standalone board pad holes
    for seg in &board.net_segments {
        for bpad in &seg.pads {
            for hole in &bpad.holes {
                holes.push(DrillHole {
                    uuid: hole.uuid,
                    x: bpad.position.x,
                    y: bpad.position.y,
                    diameter: hole.diameter,
                    plated: bpad.side == PadSide::ThroughHole,
                });
            }
        }
    }

    holes
}

/// Extract board outline polygon vertices in world coords.
fn board_outline_vertices(board: &Board) -> Option<Vec<(f64, f64)>> {
    for poly in &board.polygons {
        if poly.layer == Layer::BoardOutlines {
            let verts: Vec<(f64, f64)> = poly
                .vertices
                .iter()
                .map(|v| (v.position.x, v.position.y))
                .collect();
            if !verts.is_empty() {
                return Some(verts);
            }
        }
    }
    None
}

// ===========================================================================
// DRC engine
// ===========================================================================

/// Run all board-level DRC checks.
pub fn run_drc(
    board: &Board,
    _circuit: &Circuit,
    library: &dyn BoardLibrary,
) -> DrcResult {
    let mut diagnostics = Vec::new();

    // Collect data once
    let copper = collect_copper_objects(board, library);
    let drills = collect_drill_holes(board, library);
    let outline = board_outline_vertices(board);

    // Clearance checks
    check_copper_copper_clearance(board, &copper, &mut diagnostics);
    check_copper_board_clearance(board, &copper, outline.as_deref(), &mut diagnostics);
    check_copper_npth_clearance(board, &copper, &drills, &mut diagnostics);
    check_drill_drill_clearance(board, &drills, &mut diagnostics);
    check_drill_board_clearance(board, &drills, outline.as_deref(), &mut diagnostics);

    // Width/size checks
    check_min_trace_width(board, &mut diagnostics);
    check_min_annular_ring(board, library, &mut diagnostics);
    check_min_npth_drill(board, &drills, &mut diagnostics);
    check_min_pth_drill(board, &drills, &mut diagnostics);

    // Board-level checks
    check_unplaced_device(board, &mut diagnostics);
    check_missing_board_outline(board, &mut diagnostics);
    check_overlapping_devices(board, library, &mut diagnostics);
    check_blind_buried_vias(board, &mut diagnostics);

    let errors = diagnostics.iter().filter(|d| d.severity == Severity::Error).count();
    let warnings = diagnostics.iter().filter(|d| d.severity == Severity::Warning).count();

    DrcResult {
        passed: errors == 0,
        errors,
        warnings,
        diagnostics,
    }
}

// ===========================================================================
// Clearance checks
// ===========================================================================

/// D001: Copper-to-copper clearance violation.
fn check_copper_copper_clearance(
    board: &Board,
    copper: &[CopperObject],
    diags: &mut Vec<DrcDiagnostic>,
) {
    let min_clearance = board.drc_settings.min_copper_copper_clearance;
    let n = copper.len();

    for i in 0..n {
        for j in (i + 1)..n {
            let (obj_a, obj_b) = (&copper[i], &copper[j]);

            // Only check objects on the same layer
            let a_layers = copper_layers(obj_a);
            let b_layers = copper_layers(obj_b);
            if !share_layer(&a_layers, &b_layers) {
                continue;
            }

            let clearance = copper_object_distance(obj_a, obj_b);
            if clearance < min_clearance {
                let (loc, uuid) = copper_object_location(obj_a);
                diags.push(DrcDiagnostic {
                    rule: "D001".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Copper-to-copper clearance {:.3}mm < minimum {:.3}mm",
                        clearance, min_clearance
                    ),
                    location: Some(loc),
                    object: Some(uuid),
                });
            }
        }
    }
}

fn copper_layers(obj: &CopperObject) -> Vec<Layer> {
    match obj {
        CopperObject::TraceSegment { layer, .. } => vec![*layer],
        CopperObject::Pad { layers, .. } => layers.clone(),
    }
}

fn copper_object_location(obj: &CopperObject) -> (Position, Uuid) {
    match obj {
        CopperObject::TraceSegment { uuid, x1, y1, .. } => {
            (Position::new(*x1, *y1), *uuid)
        }
        CopperObject::Pad { uuid, x, y, .. } => {
            (Position::new(*x, *y), *uuid)
        }
    }
}

/// Compute minimum edge-to-edge distance between two copper objects.
fn copper_object_distance(a: &CopperObject, b: &CopperObject) -> f64 {
    match (a, b) {
        (
            CopperObject::TraceSegment { x1: a1x, y1: a1y, x2: a2x, y2: a2y, width: wa, .. },
            CopperObject::TraceSegment { x1: b1x, y1: b1y, x2: b2x, y2: b2y, width: wb, .. },
        ) => {
            let center_dist = segment_to_segment_distance(
                *a1x, *a1y, *a2x, *a2y,
                *b1x, *b1y, *b2x, *b2y,
            );
            (center_dist - wa / 2.0 - wb / 2.0).max(0.0)
        }
        (
            CopperObject::TraceSegment { x1, y1, x2, y2, width: w, .. },
            CopperObject::Pad { x, y, width: pw, height: ph, .. },
        ) | (
            CopperObject::Pad { x, y, width: pw, height: ph, .. },
            CopperObject::TraceSegment { x1, y1, x2, y2, width: w, .. },
        ) => {
            // Approximate pad as circle with radius = max(pw, ph) / 2
            let pad_r = pw.max(*ph) / 2.0;
            let center_dist = point_to_segment_distance(*x, *y, *x1, *y1, *x2, *y2);
            (center_dist - w / 2.0 - pad_r).max(0.0)
        }
        (
            CopperObject::Pad { x: x1, y: y1, width: w1, height: h1, .. },
            CopperObject::Pad { x: x2, y: y2, width: w2, height: h2, .. },
        ) => {
            let r1 = w1.max(*h1) / 2.0;
            let r2 = w2.max(*h2) / 2.0;
            let center_dist = point_distance(*x1, *y1, *x2, *y2);
            (center_dist - r1 - r2).max(0.0)
        }
    }
}

/// D002: Copper-to-board-edge clearance.
fn check_copper_board_clearance(
    board: &Board,
    copper: &[CopperObject],
    outline: Option<&[(f64, f64)]>,
    diags: &mut Vec<DrcDiagnostic>,
) {
    let outline = match outline {
        Some(o) if !o.is_empty() => o,
        _ => return, // No outline → D011 handles this
    };
    let min_clearance = board.drc_settings.min_copper_board_clearance;

    for obj in copper {
        let clearance = match obj {
            CopperObject::TraceSegment { x1, y1, x2, y2, width, .. } => {
                let edge_dist = segment_to_polygon_edge_distance(*x1, *y1, *x2, *y2, outline);
                (edge_dist - width / 2.0).max(0.0)
            }
            CopperObject::Pad { x, y, width, height, .. } => {
                let r = width.max(*height) / 2.0;
                let edge_dist = point_to_polygon_edge_distance(*x, *y, outline);
                (edge_dist - r).max(0.0)
            }
        };

        if clearance < min_clearance {
            let (loc, uuid) = copper_object_location(obj);
            diags.push(DrcDiagnostic {
                rule: "D002".into(),
                severity: Severity::Error,
                message: format!(
                    "Copper-to-board-edge clearance {:.3}mm < minimum {:.3}mm",
                    clearance, min_clearance
                ),
                location: Some(loc),
                object: Some(uuid),
            });
        }
    }
}

/// D003: Copper-to-NPTH clearance.
fn check_copper_npth_clearance(
    board: &Board,
    copper: &[CopperObject],
    drills: &[DrillHole],
    diags: &mut Vec<DrcDiagnostic>,
) {
    let min_clearance = board.drc_settings.min_copper_npth_clearance;
    let npth: Vec<&DrillHole> = drills.iter().filter(|d| !d.plated).collect();

    for obj in copper {
        for hole in &npth {
            let clearance = match obj {
                CopperObject::TraceSegment { x1, y1, x2, y2, width, .. } => {
                    let d = point_to_segment_distance(hole.x, hole.y, *x1, *y1, *x2, *y2);
                    (d - width / 2.0 - hole.diameter / 2.0).max(0.0)
                }
                CopperObject::Pad { x, y, width, height, .. } => {
                    let r = width.max(*height) / 2.0;
                    let d = point_distance(*x, *y, hole.x, hole.y);
                    (d - r - hole.diameter / 2.0).max(0.0)
                }
            };

            if clearance < min_clearance {
                let (loc, uuid) = copper_object_location(obj);
                diags.push(DrcDiagnostic {
                    rule: "D003".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Copper-to-NPTH clearance {:.3}mm < minimum {:.3}mm",
                        clearance, min_clearance
                    ),
                    location: Some(loc),
                    object: Some(uuid),
                });
            }
        }
    }
}

/// D004: Drill-to-drill clearance.
fn check_drill_drill_clearance(
    board: &Board,
    drills: &[DrillHole],
    diags: &mut Vec<DrcDiagnostic>,
) {
    let min_clearance = board.drc_settings.min_drill_drill_clearance;
    let n = drills.len();

    for i in 0..n {
        for j in (i + 1)..n {
            let a = &drills[i];
            let b = &drills[j];
            let edge_dist = point_distance(a.x, a.y, b.x, b.y)
                - a.diameter / 2.0
                - b.diameter / 2.0;
            let edge_dist = edge_dist.max(0.0);

            if edge_dist < min_clearance {
                diags.push(DrcDiagnostic {
                    rule: "D004".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Drill-to-drill clearance {:.3}mm < minimum {:.3}mm",
                        edge_dist, min_clearance
                    ),
                    location: Some(Position::new(a.x, a.y)),
                    object: Some(a.uuid),
                });
            }
        }
    }
}

/// D005: Drill-to-board-edge clearance.
fn check_drill_board_clearance(
    board: &Board,
    drills: &[DrillHole],
    outline: Option<&[(f64, f64)]>,
    diags: &mut Vec<DrcDiagnostic>,
) {
    let outline = match outline {
        Some(o) if !o.is_empty() => o,
        _ => return,
    };
    let min_clearance = board.drc_settings.min_drill_board_clearance;

    for hole in drills {
        let edge_dist = point_to_polygon_edge_distance(hole.x, hole.y, outline)
            - hole.diameter / 2.0;
        let edge_dist = edge_dist.max(0.0);

        if edge_dist < min_clearance {
            diags.push(DrcDiagnostic {
                rule: "D005".into(),
                severity: Severity::Error,
                message: format!(
                    "Drill-to-board-edge clearance {:.3}mm < minimum {:.3}mm",
                    edge_dist, min_clearance
                ),
                location: Some(Position::new(hole.x, hole.y)),
                object: Some(hole.uuid),
            });
        }
    }
}

// ===========================================================================
// Width / size checks
// ===========================================================================

/// D006: Minimum trace width.
fn check_min_trace_width(board: &Board, diags: &mut Vec<DrcDiagnostic>) {
    let min_width = board.drc_settings.min_copper_width;

    for seg in &board.net_segments {
        for trace in &seg.traces {
            if trace.width < min_width {
                diags.push(DrcDiagnostic {
                    rule: "D006".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Trace width {:.3}mm < minimum {:.3}mm",
                        trace.width, min_width
                    ),
                    location: None,
                    object: Some(trace.uuid),
                });
            }
        }
    }
}

/// D007: Minimum annular ring.
fn check_min_annular_ring(
    board: &Board,
    library: &dyn BoardLibrary,
    diags: &mut Vec<DrcDiagnostic>,
) {
    let min_ring = board.drc_settings.min_annular_ring;

    // Vias
    for seg in &board.net_segments {
        for via in &seg.vias {
            let outer = via_outer_diameter(via, board);
            let ring = (outer - via.drill) / 2.0;
            if ring < min_ring {
                diags.push(DrcDiagnostic {
                    rule: "D007".into(),
                    severity: Severity::Error,
                    message: format!(
                        "Via annular ring {:.3}mm < minimum {:.3}mm (outer={:.3}mm, drill={:.3}mm)",
                        ring, min_ring, outer, via.drill
                    ),
                    location: Some(via.position),
                    object: Some(via.uuid),
                });
            }
        }
    }

    // PTH pads from devices
    for bd in &board.devices {
        if let Some(lib_dev) = library.get_device(&bd.lib_device) {
            if let Some(pkg) = library.get_package(&lib_dev.package) {
                if let Some(fp) = pkg.footprints.iter().find(|f| f.uuid == bd.lib_footprint) {
                    for fp_pad in &fp.pads {
                        for hole in &fp_pad.holes {
                            // Annular ring: (pad_width - hole_diameter) / 2
                            let ring = (fp_pad.width.min(fp_pad.height) - hole.diameter) / 2.0;
                            if ring < min_ring {
                                let (wx, wy) = transform_point(
                                    fp_pad.position.x, fp_pad.position.y, bd,
                                );
                                diags.push(DrcDiagnostic {
                                    rule: "D007".into(),
                                    severity: Severity::Error,
                                    message: format!(
                                        "PTH pad annular ring {:.3}mm < minimum {:.3}mm",
                                        ring, min_ring
                                    ),
                                    location: Some(Position::new(wx, wy)),
                                    object: Some(fp_pad.uuid),
                                });
                            }
                        }
                    }
                }
            }
        }
    }
}

/// D008: Minimum NPTH drill diameter.
fn check_min_npth_drill(
    board: &Board,
    drills: &[DrillHole],
    diags: &mut Vec<DrcDiagnostic>,
) {
    let min_d = board.drc_settings.min_npth_drill_diameter;

    for hole in drills {
        if !hole.plated && hole.diameter < min_d {
            diags.push(DrcDiagnostic {
                rule: "D008".into(),
                severity: Severity::Error,
                message: format!(
                    "NPTH drill diameter {:.3}mm < minimum {:.3}mm",
                    hole.diameter, min_d
                ),
                location: Some(Position::new(hole.x, hole.y)),
                object: Some(hole.uuid),
            });
        }
    }
}

/// D009: Minimum PTH drill diameter.
fn check_min_pth_drill(
    board: &Board,
    drills: &[DrillHole],
    diags: &mut Vec<DrcDiagnostic>,
) {
    let min_d = board.drc_settings.min_pth_drill_diameter;

    for hole in drills {
        if hole.plated && hole.diameter < min_d {
            diags.push(DrcDiagnostic {
                rule: "D009".into(),
                severity: Severity::Error,
                message: format!(
                    "PTH drill diameter {:.3}mm < minimum {:.3}mm",
                    hole.diameter, min_d
                ),
                location: Some(Position::new(hole.x, hole.y)),
                object: Some(hole.uuid),
            });
        }
    }
}

// ===========================================================================
// Board-level checks
// ===========================================================================

/// D010: Unplaced device — device at (0, 0) likely not intentionally placed.
fn check_unplaced_device(board: &Board, diags: &mut Vec<DrcDiagnostic>) {
    for bd in &board.devices {
        if bd.position.x == 0.0 && bd.position.y == 0.0 {
            diags.push(DrcDiagnostic {
                rule: "D010".into(),
                severity: Severity::Warning,
                message: format!(
                    "Device at position (0, 0) — likely unplaced"
                ),
                location: Some(bd.position),
                object: Some(bd.component),
            });
        }
    }
}

/// D011: Missing board outline.
fn check_missing_board_outline(board: &Board, diags: &mut Vec<DrcDiagnostic>) {
    let has_outline = board
        .polygons
        .iter()
        .any(|p| p.layer == Layer::BoardOutlines && !p.vertices.is_empty());

    if !has_outline {
        diags.push(DrcDiagnostic {
            rule: "D011".into(),
            severity: Severity::Error,
            message: "No board outline polygon found".into(),
            location: None,
            object: None,
        });
    }
}

/// D012: Overlapping devices — check if any two device footprint bounds overlap.
fn check_overlapping_devices(
    board: &Board,
    library: &dyn BoardLibrary,
    diags: &mut Vec<DrcDiagnostic>,
) {
    // Compute axis-aligned bounding boxes for each device in world space
    let bboxes: Vec<(Uuid, f64, f64, f64, f64)> = board
        .devices
        .iter()
        .filter_map(|bd| {
            let lib_dev = library.get_device(&bd.lib_device)?;
            let pkg = library.get_package(&lib_dev.package)?;
            let fp = pkg.footprints.iter().find(|f| f.uuid == bd.lib_footprint)?;

            // Collect all pad and polygon positions to compute bounds
            let mut points: Vec<(f64, f64)> = Vec::new();

            for pad in &fp.pads {
                let (wx, wy) = transform_point(pad.position.x, pad.position.y, bd);
                points.push((wx - pad.width / 2.0, wy - pad.height / 2.0));
                points.push((wx + pad.width / 2.0, wy + pad.height / 2.0));
            }

            for poly in &fp.polygons {
                for v in &poly.vertices {
                    let (wx, wy) = transform_point(v.position.x, v.position.y, bd);
                    points.push((wx, wy));
                }
            }

            if points.is_empty() {
                return None;
            }

            let min_x = points.iter().map(|p| p.0).fold(f64::INFINITY, f64::min);
            let max_x = points.iter().map(|p| p.0).fold(f64::NEG_INFINITY, f64::max);
            let min_y = points.iter().map(|p| p.1).fold(f64::INFINITY, f64::min);
            let max_y = points.iter().map(|p| p.1).fold(f64::NEG_INFINITY, f64::max);

            Some((bd.component, min_x, min_y, max_x, max_y))
        })
        .collect();

    let n = bboxes.len();
    for i in 0..n {
        for j in (i + 1)..n {
            let (uuid_a, ax1, ay1, ax2, ay2) = bboxes[i];
            let (_, bx1, by1, bx2, by2) = bboxes[j];

            // AABB overlap check
            if ax1 < bx2 && ax2 > bx1 && ay1 < by2 && ay2 > by1 {
                diags.push(DrcDiagnostic {
                    rule: "D012".into(),
                    severity: Severity::Warning,
                    message: "Overlapping device footprint bounds".into(),
                    location: Some(Position::new(
                        (ax1 + ax2) / 2.0,
                        (ay1 + ay2) / 2.0,
                    )),
                    object: Some(uuid_a),
                });
            }
        }
    }
}

/// D013: Blind/buried via used when not allowed.
fn check_blind_buried_vias(board: &Board, diags: &mut Vec<DrcDiagnostic>) {
    let stack = copper_layer_stack(board);
    let top = stack.first().copied();
    let bot = stack.last().copied();

    for seg in &board.net_segments {
        for via in &seg.vias {
            let is_through = Some(via.from_layer) == top && Some(via.to_layer) == bot
                || Some(via.from_layer) == bot && Some(via.to_layer) == top;

            if is_through {
                continue;
            }

            // Determine if blind or buried
            let touches_outer = Some(via.from_layer) == top
                || Some(via.from_layer) == bot
                || Some(via.to_layer) == top
                || Some(via.to_layer) == bot;

            if touches_outer {
                // Blind via
                if !board.drc_settings.blind_vias_allowed {
                    diags.push(DrcDiagnostic {
                        rule: "D013".into(),
                        severity: Severity::Error,
                        message: "Blind via used but blind vias are not allowed".into(),
                        location: Some(via.position),
                        object: Some(via.uuid),
                    });
                }
            } else {
                // Buried via
                if !board.drc_settings.buried_vias_allowed {
                    diags.push(DrcDiagnostic {
                        rule: "D013".into(),
                        severity: Severity::Error,
                        message: "Buried via used but buried vias are not allowed".into(),
                        location: Some(via.position),
                        object: Some(via.uuid),
                    });
                }
            }
        }
    }
}

// ===========================================================================
// Tests
// ===========================================================================

#[cfg(test)]
mod tests;
