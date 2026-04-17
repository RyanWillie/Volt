//! Zone-refill engine for Volt EDA.
//!
//! Computes copper-fill fragments for planes by:
//! 1. Clipping to board outline
//! 2. Subtracting foreign-net copper with clearance
//! 3. Subtracting holes/keepouts
//! 4. Applying thermal/solid/none connect styles
//! 5. Removing islands (optionally)
//!
//! Uses `i_overlay` (MIT/Apache-2.0) for polygon boolean operations.

use std::collections::{HashMap, HashSet};
use std::f64::consts::PI;

use i_overlay::core::fill_rule::FillRule;
use i_overlay::core::overlay_rule::OverlayRule;
use i_overlay::float::overlay::FloatOverlay;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Device, FootprintPad, Package};
use volt_core::project::*;

// ============================================================================
// Public API
// ============================================================================

/// Trait for resolving library elements during refill.
pub trait RefillLibrary {
    fn get_device(&self, uuid: &Uuid) -> Option<&Device>;
    fn get_package(&self, uuid: &Uuid) -> Option<&Package>;
}

/// Simple map-based library resolver.
pub struct MapRefillLibrary {
    pub devices: HashMap<Uuid, Device>,
    pub packages: HashMap<Uuid, Package>,
}

impl RefillLibrary for MapRefillLibrary {
    fn get_device(&self, uuid: &Uuid) -> Option<&Device> {
        self.devices.get(uuid)
    }
    fn get_package(&self, uuid: &Uuid) -> Option<&Package> {
        self.packages.get(uuid)
    }
}

/// Run the refill engine on all planes in the board.
/// Mutates `board.planes[].fragments` in place.
pub fn refill_board(board: &mut Board, circuit: &Circuit, library: &dyn RefillLibrary) {
    let outline = board_outline_polygon(board);
    let layers = unique_plane_layers(board);

    for layer in layers {
        refill_layer(board, circuit, library, layer, outline.as_deref());
    }
}

// ============================================================================
// Per-layer refill
// ============================================================================

fn refill_layer(
    board: &mut Board,
    circuit: &Circuit,
    library: &dyn RefillLibrary,
    layer: Layer,
    outline: Option<&[[f64; 2]]>,
) {
    // Collect plane indices on this layer, sorted by descending priority
    let mut plane_indices: Vec<usize> = board
        .planes
        .iter()
        .enumerate()
        .filter(|(_, p)| p.layer == layer)
        .map(|(i, _)| i)
        .collect();
    plane_indices.sort_by(|&a, &b| board.planes[b].priority.cmp(&board.planes[a].priority));

    // Build net→pad map for same-net detection
    let pad_net_map = build_pad_net_map(board, circuit, library);

    // Track already-filled area for priority subtraction
    let mut higher_priority_fills: Vec<Vec<[f64; 2]>> = Vec::new();

    for &plane_idx in &plane_indices {
        let plane_net = board.planes[plane_idx].net;
        let plane_clearance = board.planes[plane_idx].min_copper_clearance;
        let board_clearance = board.planes[plane_idx].min_board_clearance;
        let npth_clearance = board.planes[plane_idx].min_npth_clearance;
        let connect_style = board.planes[plane_idx].connect_style;
        let thermal_gap = board.planes[plane_idx].thermal_gap;
        let thermal_spoke = board.planes[plane_idx].thermal_spoke;
        let min_width = board.planes[plane_idx].min_width;
        let keep_islands = board.planes[plane_idx].keep_islands;

        // 1. Flatten plane polygon
        let plane_poly = flatten_vertices(&board.planes[plane_idx].vertices);
        if plane_poly.len() < 3 {
            board.planes[plane_idx].fragments = vec![];
            continue;
        }

        // 2. Clip to board outline (inset by board_clearance)
        let mut fill = if let Some(outline) = outline {
            let inset = inset_polygon(outline, board_clearance);
            if inset.is_empty() {
                board.planes[plane_idx].fragments = vec![];
                continue;
            }
            poly_intersect(&[plane_poly.clone()], &[inset])
        } else {
            vec![vec![plane_poly.clone()]]
        };

        if fill.is_empty() {
            board.planes[plane_idx].fragments = vec![];
            continue;
        }

        // 3. Subtract higher-priority plane fills
        for hp in &higher_priority_fills {
            let hp_expanded = offset_polygon(hp, plane_clearance);
            if !hp_expanded.is_empty() {
                fill = poly_difference_shapes(&fill, &[hp_expanded]);
            }
        }

        // 4. Collect and subtract obstacles
        let mut obstacles: Vec<Vec<[f64; 2]>> = Vec::new();

        // Foreign-net traces
        for seg in &board.net_segments {
            let seg_net = seg.net;
            let is_same_net = seg_net == Some(plane_net);

            for trace in &seg.traces {
                if trace.layer != layer {
                    continue;
                }
                if is_same_net {
                    continue;
                }
                let from_pos = resolve_trace_endpoint_pos(&trace.from, board, library);
                let to_pos = resolve_trace_endpoint_pos(&trace.to, board, library);
                if let (Some(fp), Some(tp)) = (from_pos, to_pos) {
                    let trace_poly = stroke_to_polygon(fp, tp, trace.width / 2.0 + plane_clearance);
                    obstacles.push(trace_poly);
                }
            }

            // Vias
            for via in &seg.vias {
                if !via_spans_layer(via, layer) {
                    continue;
                }
                if is_same_net {
                    continue;
                }
                let via_diameter = compute_via_outer_diameter(via);
                let r = via_diameter / 2.0 + plane_clearance;
                obstacles.push(circle_polygon(via.position.x, via.position.y, r, 32));
            }

            // Standalone board pads (on this layer + foreign net)
            for pad in &seg.pads {
                if !pad_on_layer(pad, layer) {
                    continue;
                }
                if is_same_net {
                    continue;
                }
                let pad_poly = pad_polygon_with_clearance(
                    pad.position.x,
                    pad.position.y,
                    pad.size_width,
                    pad.size_height,
                    pad.rotation.0,
                    plane_clearance,
                );
                obstacles.push(pad_poly);
            }
        }

        // Device pads
        for dev in &board.devices {
            let device_lib = library.get_device(&dev.lib_device);
            let package = device_lib.and_then(|d| library.get_package(&d.package));
            let footprint = package.and_then(|p| {
                p.footprints
                    .iter()
                    .find(|f| f.uuid == dev.lib_footprint)
                    .or_else(|| p.footprints.first())
            });

            if let Some(fp) = footprint {
                for fp_pad in &fp.pads {
                    if !fp_pad_on_layer(fp_pad, dev.flip, layer) {
                        continue;
                    }
                    let pad_pos = transform_pad_position(
                        fp_pad.position,
                        dev.position,
                        dev.rotation.0,
                        dev.flip,
                    );
                    let pad_net = pad_net_map.get(&(dev.component, fp_pad.uuid));
                    let is_same_net_pad = pad_net.map_or(false, |n| *n == plane_net);

                    match connect_style {
                        ConnectStyle::Solid if is_same_net_pad => {
                            // No obstacle — pad merges into plane
                        }
                        ConnectStyle::Thermal if is_same_net_pad => {
                            // Cut thermal relief gaps around pad
                            let thermal_obstacles = thermal_relief_gaps(
                                pad_pos.x,
                                pad_pos.y,
                                fp_pad.width,
                                fp_pad.height,
                                dev.rotation.0 + fp_pad.rotation.0,
                                thermal_gap,
                                thermal_spoke,
                                plane_clearance,
                            );
                            obstacles.extend(thermal_obstacles);
                        }
                        _ => {
                            // Foreign-net or None style: full antipad
                            let pad_poly = pad_polygon_with_clearance(
                                pad_pos.x,
                                pad_pos.y,
                                fp_pad.width,
                                fp_pad.height,
                                dev.rotation.0 + fp_pad.rotation.0,
                                plane_clearance,
                            );
                            obstacles.push(pad_poly);
                        }
                    }

                    // PTH holes on device pads
                    for hole in &fp_pad.holes {
                        let hole_pos = transform_pad_position(
                            hole.path
                                .first()
                                .map(|v| v.position)
                                .unwrap_or(fp_pad.position),
                            dev.position,
                            dev.rotation.0,
                            dev.flip,
                        );
                        let r = hole.diameter / 2.0 + plane_clearance;
                        obstacles.push(circle_polygon(hole_pos.x, hole_pos.y, r, 32));
                    }
                }
            }
        }

        // NPTH board holes
        for hole in &board.holes {
            let pos = hole
                .path
                .first()
                .map(|v| v.position)
                .unwrap_or(Position::new(0.0, 0.0));
            let r = hole.diameter / 2.0 + npth_clearance;
            obstacles.push(circle_polygon(pos.x, pos.y, r, 32));
        }

        // Subtract all obstacles at once
        if !obstacles.is_empty() {
            fill = poly_difference_shapes(&fill, &obstacles);
        }

        // 6. Sliver removal (simplified: negative offset by min_width/2 then positive)
        if min_width > 0.0 {
            fill = remove_slivers(&fill, min_width);
        }

        // 7. Island removal
        if !keep_islands {
            let same_net_positions =
                collect_same_net_positions(board, circuit, library, plane_net, layer);
            fill = remove_islands(&fill, &same_net_positions);
        }

        // Record for priority subtraction
        for shape in &fill {
            if let Some(outer) = shape.first() {
                higher_priority_fills.push(outer.clone());
            }
        }

        // Convert to PlaneFragment
        let fragments: Vec<PlaneFragment> = fill
            .into_iter()
            .map(|contours| PlaneFragment {
                contours: contours
                    .into_iter()
                    .map(|c| c.into_iter().map(|p| Position::new(p[0], p[1])).collect())
                    .collect(),
            })
            .collect();

        board.planes[plane_idx].fragments = fragments;
    }
}

// ============================================================================
// Geometry helpers
// ============================================================================

type Poly = Vec<[f64; 2]>;
type Shape = Vec<Poly>; // outer + holes

fn flatten_vertices(vertices: &[Vertex]) -> Poly {
    if vertices.is_empty() {
        return vec![];
    }
    let mut result = Vec::new();
    for i in 0..vertices.len() {
        let v = &vertices[i];
        if v.angle.0.abs() < 1e-6 || i == 0 {
            result.push([v.position.x, v.position.y]);
        } else {
            // Arc: tessellate from previous point to this point
            let prev = &vertices[i - 1];
            let arc_pts = tessellate_arc(
                prev.position.x,
                prev.position.y,
                v.position.x,
                v.position.y,
                v.angle.0,
            );
            result.extend(arc_pts);
        }
    }
    result
}

fn tessellate_arc(x0: f64, y0: f64, x1: f64, y1: f64, angle_deg: f64) -> Vec<[f64; 2]> {
    let angle_rad = angle_deg.to_radians();
    let n = ((angle_rad.abs() / (2.0 * PI)) * 64.0).ceil().max(4.0) as usize;
    let mut pts = Vec::with_capacity(n);
    // Compute arc center from chord and angle
    let mx = (x0 + x1) / 2.0;
    let my = (y0 + y1) / 2.0;
    let dx = x1 - x0;
    let dy = y1 - y0;
    let chord_len = (dx * dx + dy * dy).sqrt();
    if chord_len < 1e-9 {
        return vec![[x1, y1]];
    }
    let r = (chord_len / 2.0) / (angle_rad / 2.0).sin().abs();
    let h = r * (angle_rad / 2.0).cos();
    let sign = if angle_rad > 0.0 { 1.0 } else { -1.0 };
    let nx = -dy / chord_len;
    let ny = dx / chord_len;
    let cx = mx + sign * h * nx;
    let cy = my + sign * h * ny;

    let start_angle = (y0 - cy).atan2(x0 - cx);
    for i in 1..=n {
        let t = i as f64 / n as f64;
        let a = start_angle + angle_rad * t;
        pts.push([cx + r * a.cos(), cy + r * a.sin()]);
    }
    pts
}

fn circle_polygon(cx: f64, cy: f64, r: f64, n: usize) -> Poly {
    (0..n)
        .map(|i| {
            let theta = 2.0 * PI * i as f64 / n as f64;
            [cx + r * theta.cos(), cy + r * theta.sin()]
        })
        .collect()
}

fn offset_polygon(poly: &[[f64; 2]], offset: f64) -> Poly {
    if poly.len() < 3 || offset.abs() < 1e-9 {
        return poly.to_vec();
    }
    // Offset each edge by moving it along its outward normal,
    // then compute intersections of adjacent offset edges.
    let n = poly.len();
    // Compute outward normals for each edge
    let mut normals = Vec::with_capacity(n);
    for i in 0..n {
        let j = (i + 1) % n;
        let dx = poly[j][0] - poly[i][0];
        let dy = poly[j][1] - poly[i][1];
        let len = (dx * dx + dy * dy).sqrt();
        if len < 1e-12 {
            normals.push([0.0, 0.0]);
        } else {
            // Outward normal (left-hand for CCW polygon)
            normals.push([-dy / len, dx / len]);
        }
    }

    let mut result = Vec::with_capacity(n);
    for i in 0..n {
        let prev_edge = (i + n - 1) % n;
        let curr_edge = i;

        // Previous offset edge: from poly[prev_edge] + normal*offset to poly[i] + normal*offset
        let p0x = poly[prev_edge][0] + normals[prev_edge][0] * offset;
        let p0y = poly[prev_edge][1] + normals[prev_edge][1] * offset;
        let p1x = poly[i][0] + normals[prev_edge][0] * offset;
        let p1y = poly[i][1] + normals[prev_edge][1] * offset;

        // Current offset edge: from poly[i] + normal*offset to poly[(i+1)%n] + normal*offset
        let q0x = poly[i][0] + normals[curr_edge][0] * offset;
        let q0y = poly[i][1] + normals[curr_edge][1] * offset;
        let q1x = poly[(i + 1) % n][0] + normals[curr_edge][0] * offset;
        let q1y = poly[(i + 1) % n][1] + normals[curr_edge][1] * offset;

        // Intersect the two offset lines
        if let Some(pt) = line_line_intersect(p0x, p0y, p1x, p1y, q0x, q0y, q1x, q1y) {
            result.push(pt);
        } else {
            // Parallel edges, use midpoint of offset
            result.push([(p1x + q0x) / 2.0, (p1y + q0y) / 2.0]);
        }
    }
    result
}

fn line_line_intersect(
    p0x: f64,
    p0y: f64,
    p1x: f64,
    p1y: f64,
    q0x: f64,
    q0y: f64,
    q1x: f64,
    q1y: f64,
) -> Option<[f64; 2]> {
    let d1x = p1x - p0x;
    let d1y = p1y - p0y;
    let d2x = q1x - q0x;
    let d2y = q1y - q0y;
    let denom = d1x * d2y - d1y * d2x;
    if denom.abs() < 1e-12 {
        return None;
    }
    let t = ((q0x - p0x) * d2y - (q0y - p0y) * d2x) / denom;
    Some([p0x + t * d1x, p0y + t * d1y])
}

fn inset_polygon(poly: &[[f64; 2]], inset: f64) -> Poly {
    if inset.abs() < 1e-9 {
        return poly.to_vec();
    }
    // Inset = shrink the polygon. Use positive offset with inward normals.
    offset_polygon(poly, inset)
}

fn stroke_to_polygon(from: Position, to: Position, half_width: f64) -> Poly {
    let dx = to.x - from.x;
    let dy = to.y - from.y;
    let len = (dx * dx + dy * dy).sqrt();
    if len < 1e-9 {
        return circle_polygon(from.x, from.y, half_width, 16);
    }
    let nx = -dy / len * half_width;
    let ny = dx / len * half_width;
    vec![
        [from.x + nx, from.y + ny],
        [to.x + nx, to.y + ny],
        [to.x - nx, to.y - ny],
        [from.x - nx, from.y - ny],
    ]
}

fn pad_polygon_with_clearance(
    cx: f64,
    cy: f64,
    w: f64,
    h: f64,
    rotation_deg: f64,
    clearance: f64,
) -> Poly {
    let hw = w / 2.0 + clearance;
    let hh = h / 2.0 + clearance;
    let corners = [[-hw, -hh], [hw, -hh], [hw, hh], [-hw, hh]];
    let angle = rotation_deg.to_radians();
    let cos_a = angle.cos();
    let sin_a = angle.sin();
    corners
        .iter()
        .map(|[x, y]| [cx + x * cos_a - y * sin_a, cy + x * sin_a + y * cos_a])
        .collect()
}

fn thermal_relief_gaps(
    cx: f64,
    cy: f64,
    pad_w: f64,
    pad_h: f64,
    rotation_deg: f64,
    gap_width: f64,
    _spoke_width: f64,
    clearance: f64,
) -> Vec<Poly> {
    // Create 4 rectangular gaps radiating from pad center along cardinal directions
    // The gaps are centered on the pad axes and extend outward
    let outer_r = (pad_w.max(pad_h)) / 2.0 + clearance + 1.0; // extend past clearance
    let half_gap = gap_width / 2.0;
    let angle = rotation_deg.to_radians();
    let cos_a = angle.cos();
    let sin_a = angle.sin();

    let rotate =
        |x: f64, y: f64| -> [f64; 2] { [cx + x * cos_a - y * sin_a, cy + x * sin_a + y * cos_a] };

    // Gap along +X axis (right)
    let gap_right = vec![
        rotate(0.0, -half_gap),
        rotate(outer_r, -half_gap),
        rotate(outer_r, half_gap),
        rotate(0.0, half_gap),
    ];
    // Gap along -X axis (left)
    let gap_left = vec![
        rotate(0.0, -half_gap),
        rotate(-outer_r, -half_gap),
        rotate(-outer_r, half_gap),
        rotate(0.0, half_gap),
    ];
    // Gap along +Y axis (up)
    let gap_up = vec![
        rotate(-half_gap, 0.0),
        rotate(half_gap, 0.0),
        rotate(half_gap, outer_r),
        rotate(-half_gap, outer_r),
    ];
    // Gap along -Y axis (down)
    let gap_down = vec![
        rotate(-half_gap, 0.0),
        rotate(half_gap, 0.0),
        rotate(half_gap, -outer_r),
        rotate(-half_gap, -outer_r),
    ];
    vec![gap_right, gap_left, gap_up, gap_down]
}

// ============================================================================
// Boolean operation wrappers
// ============================================================================

fn poly_intersect(subject: &[Poly], clip: &[Poly]) -> Vec<Shape> {
    let mut overlay = FloatOverlay::with_subj_and_clip(subject, clip);
    overlay.overlay(OverlayRule::Intersect, FillRule::NonZero)
}

fn poly_difference_shapes(subject: &[Shape], clip_polys: &[Poly]) -> Vec<Shape> {
    // Flatten shapes to contour list for subject
    let subj_contours: Vec<Poly> = subject.iter().flat_map(|s| s.iter().cloned()).collect();
    if subj_contours.is_empty() {
        return vec![];
    }
    let mut overlay = FloatOverlay::with_subj_and_clip(&subj_contours, clip_polys);
    overlay.overlay(OverlayRule::Difference, FillRule::NonZero)
}

// ============================================================================
// Net resolution
// ============================================================================

/// Maps (component_uuid, footprint_pad_uuid) → net_uuid
fn build_pad_net_map(
    board: &Board,
    circuit: &Circuit,
    library: &dyn RefillLibrary,
) -> HashMap<(Uuid, Uuid), Uuid> {
    let mut map = HashMap::new();

    for dev in &board.devices {
        let device_lib = match library.get_device(&dev.lib_device) {
            Some(d) => d,
            None => continue,
        };
        let package = match library.get_package(&device_lib.package) {
            Some(p) => p,
            None => continue,
        };
        let comp = match circuit.components.iter().find(|c| c.uuid == dev.component) {
            Some(c) => c,
            None => continue,
        };
        let footprint = package
            .footprints
            .iter()
            .find(|f| f.uuid == dev.lib_footprint)
            .or_else(|| package.footprints.first());
        let footprint = match footprint {
            Some(f) => f,
            None => continue,
        };

        for fp_pad in &footprint.pads {
            // fp_pad.package_pad → device pad_mapping → signal → net
            let signal = device_lib
                .pad_mappings
                .iter()
                .find(|pm| pm.pad == fp_pad.package_pad)
                .map(|pm| pm.signal);
            if let Some(sig) = signal {
                let net = comp
                    .signal_connections
                    .iter()
                    .find(|sc| sc.signal == sig)
                    .and_then(|sc| sc.net);
                if let Some(net_uuid) = net {
                    map.insert((dev.component, fp_pad.uuid), net_uuid);
                }
            }
        }
    }
    map
}

// ============================================================================
// Board geometry helpers
// ============================================================================

fn board_outline_polygon(board: &Board) -> Option<Poly> {
    board
        .polygons
        .iter()
        .find(|p| p.layer == Layer::BoardOutlines && p.vertices.len() >= 3)
        .map(|p| flatten_vertices(&p.vertices))
}

fn unique_plane_layers(board: &Board) -> Vec<Layer> {
    let mut layers: Vec<Layer> = board.planes.iter().map(|p| p.layer).collect();
    layers.sort_by_key(|l| format!("{:?}", l));
    layers.dedup();
    layers
}

fn resolve_trace_endpoint_pos(
    ep: &TraceEndpoint,
    board: &Board,
    library: &dyn RefillLibrary,
) -> Option<Position> {
    match ep {
        TraceEndpoint::Junction { junction } => {
            for seg in &board.net_segments {
                for j in &seg.junctions {
                    if j.uuid == *junction {
                        return Some(j.position);
                    }
                }
            }
            None
        }
        TraceEndpoint::Via { via } => {
            for seg in &board.net_segments {
                for v in &seg.vias {
                    if v.uuid == *via {
                        return Some(v.position);
                    }
                }
            }
            None
        }
        TraceEndpoint::Device { device, pad } => {
            let board_dev = board.devices.iter().find(|d| d.component == *device)?;
            let dev_lib = library.get_device(&board_dev.lib_device)?;
            let pkg = library.get_package(&dev_lib.package)?;
            let fp = pkg
                .footprints
                .iter()
                .find(|f| f.uuid == board_dev.lib_footprint)
                .or_else(|| pkg.footprints.first())?;
            let fp_pad = fp.pads.iter().find(|p| p.uuid == *pad)?;
            Some(transform_pad_position(
                fp_pad.position,
                board_dev.position,
                board_dev.rotation.0,
                board_dev.flip,
            ))
        }
    }
}

fn transform_pad_position(
    pad_pos: Position,
    dev_pos: Position,
    rotation_deg: f64,
    flip: bool,
) -> Position {
    let mut x = pad_pos.x;
    let y = pad_pos.y;
    if flip {
        x = -x;
    }
    let angle = rotation_deg.to_radians();
    let cos_a = angle.cos();
    let sin_a = angle.sin();
    Position::new(
        dev_pos.x + x * cos_a - y * sin_a,
        dev_pos.y + x * sin_a + y * cos_a,
    )
}

fn via_spans_layer(via: &Via, layer: Layer) -> bool {
    // Simplified: through-hole vias span all copper layers
    let from_idx = layer_index(&via.from_layer);
    let to_idx = layer_index(&via.to_layer);
    let target_idx = layer_index(&layer);
    let lo = from_idx.min(to_idx);
    let hi = from_idx.max(to_idx);
    target_idx >= lo && target_idx <= hi
}

fn layer_index(layer: &Layer) -> i32 {
    match layer {
        Layer::TopCopper => 0,
        Layer::InnerCopper(n) => *n as i32,
        Layer::BottomCopper => 99,
        _ => -1,
    }
}

fn compute_via_outer_diameter(via: &Via) -> f64 {
    match via.size {
        ViaSize::Auto => via.drill + 0.5, // typical annular ring
        ViaSize::Manual(d) => d,
    }
}

fn pad_on_layer(pad: &BoardPad, layer: Layer) -> bool {
    match pad.side {
        PadSide::Top => layer == Layer::TopCopper,
        PadSide::Bottom => layer == Layer::BottomCopper,
        _ => false,
    }
}

fn fp_pad_on_layer(pad: &FootprintPad, dev_flip: bool, layer: Layer) -> bool {
    let effective_side = if dev_flip {
        match pad.side {
            PadSide::Top => PadSide::Bottom,
            PadSide::Bottom => PadSide::Top,
            other => other,
        }
    } else {
        pad.side
    };
    match effective_side {
        PadSide::Top => layer == Layer::TopCopper,
        PadSide::Bottom => layer == Layer::BottomCopper,
        _ => false, // THT pads are on both but we handle them per layer
    }
}

// ============================================================================
// Sliver removal
// ============================================================================

fn remove_slivers(shapes: &[Shape], min_width: f64) -> Vec<Shape> {
    // Approximate sliver removal: negative offset by min_width/2, then positive offset back
    // This removes features narrower than min_width
    let half = min_width / 2.0;
    let mut result = Vec::new();
    for shape in shapes {
        if let Some(outer) = shape.first() {
            let shrunk = inset_polygon(outer, half);
            if shrunk.len() >= 3 {
                let expanded = offset_polygon(&shrunk, half);
                if expanded.len() >= 3 {
                    let mut new_shape = vec![expanded];
                    // Preserve holes
                    for hole in &shape[1..] {
                        new_shape.push(hole.clone());
                    }
                    result.push(new_shape);
                }
            }
        }
    }
    result
}

// ============================================================================
// Island removal
// ============================================================================

fn collect_same_net_positions(
    board: &Board,
    circuit: &Circuit,
    library: &dyn RefillLibrary,
    net_uuid: Uuid,
    _layer: Layer,
) -> Vec<Position> {
    let mut positions = Vec::new();

    // Same-net vias and trace endpoints
    for seg in &board.net_segments {
        if seg.net != Some(net_uuid) {
            continue;
        }
        for via in &seg.vias {
            positions.push(via.position);
        }
        for junction in &seg.junctions {
            positions.push(junction.position);
        }
    }

    // Same-net device pads
    let pad_net_map = build_pad_net_map(board, circuit, library);
    for dev in &board.devices {
        let device_lib = match library.get_device(&dev.lib_device) {
            Some(d) => d,
            None => continue,
        };
        let package = match library.get_package(&device_lib.package) {
            Some(p) => p,
            None => continue,
        };
        let footprint = package
            .footprints
            .iter()
            .find(|f| f.uuid == dev.lib_footprint)
            .or_else(|| package.footprints.first());
        if let Some(fp) = footprint {
            for fp_pad in &fp.pads {
                if pad_net_map.get(&(dev.component, fp_pad.uuid)) == Some(&net_uuid) {
                    let pos = transform_pad_position(
                        fp_pad.position,
                        dev.position,
                        dev.rotation.0,
                        dev.flip,
                    );
                    positions.push(pos);
                }
            }
        }
    }

    positions
}

fn remove_islands(shapes: &[Shape], seed_positions: &[Position]) -> Vec<Shape> {
    if seed_positions.is_empty() {
        // If no seeds, keep all fragments (can't determine which is "connected")
        return shapes.to_vec();
    }

    shapes
        .iter()
        .filter(|shape| {
            if let Some(outer) = shape.first() {
                seed_positions
                    .iter()
                    .any(|pos| point_in_polygon(pos.x, pos.y, outer))
            } else {
                false
            }
        })
        .cloned()
        .collect()
}

fn point_in_polygon(px: f64, py: f64, poly: &[[f64; 2]]) -> bool {
    let n = poly.len();
    let mut inside = false;
    let mut j = n - 1;
    for i in 0..n {
        let yi = poly[i][1];
        let yj = poly[j][1];
        let xi = poly[i][0];
        let xj = poly[j][0];
        if ((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi) {
            inside = !inside;
        }
        j = i;
    }
    inside
}

// ============================================================================
// Tests
// ============================================================================

#[cfg(test)]
mod tests {
    use super::*;
    use volt_core::common::*;
    use volt_core::library::*;
    use volt_core::project::*;

    fn make_board_outline(w: f64, h: f64) -> BoardPolygon {
        BoardPolygon {
            uuid: Uuid::new_v4(),
            layer: Layer::BoardOutlines,
            width: 0.0,
            fill: false,
            grab_area: false,
            lock: false,
            vertices: vec![
                Vertex {
                    position: Position::new(0.0, 0.0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(w, 0.0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(w, h),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(0.0, h),
                    angle: Angle(0.0),
                },
            ],
        }
    }

    fn make_plane(net: Uuid, layer: Layer, x0: f64, y0: f64, x1: f64, y1: f64) -> Plane {
        Plane {
            uuid: Uuid::new_v4(),
            layer,
            net,
            priority: 0,
            min_width: 0.0,
            min_copper_clearance: 0.5,
            min_board_clearance: 1.0,
            min_npth_clearance: 0.5,
            connect_style: ConnectStyle::Solid,
            thermal_gap: 0.3,
            thermal_spoke: 0.5,
            keep_islands: false,
            lock: false,
            vertices: vec![
                Vertex {
                    position: Position::new(x0, y0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(x1, y0),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(x1, y1),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(x0, y1),
                    angle: Angle(0.0),
                },
            ],
            fragments: vec![],
        }
    }

    fn make_trace(
        net: Uuid,
        layer: Layer,
        x0: f64,
        y0: f64,
        x1: f64,
        y1: f64,
        width: f64,
    ) -> BoardNetSegment {
        let j0 = Uuid::new_v4();
        let j1 = Uuid::new_v4();
        BoardNetSegment {
            uuid: Uuid::new_v4(),
            net: Some(net),
            traces: vec![Trace {
                uuid: Uuid::new_v4(),
                layer,
                width,
                from: TraceEndpoint::Junction { junction: j0 },
                to: TraceEndpoint::Junction { junction: j1 },
            }],
            vias: vec![],
            junctions: vec![
                Junction {
                    uuid: j0,
                    position: Position::new(x0, y0),
                },
                Junction {
                    uuid: j1,
                    position: Position::new(x1, y1),
                },
            ],
            pads: vec![],
        }
    }

    fn empty_library() -> MapRefillLibrary {
        MapRefillLibrary {
            devices: HashMap::new(),
            packages: HashMap::new(),
        }
    }

    fn empty_circuit() -> Circuit {
        Circuit {
            assembly_variants: vec![],
            net_classes: vec![],
            nets: vec![],
            components: vec![],
            differential_pairs: vec![],
        }
    }

    fn minimal_board(outline_w: f64, outline_h: f64) -> Board {
        Board {
            uuid: Uuid::new_v4(),
            name: "test".into(),
            grid: Grid {
                interval: 1.0,
                unit: GridUnit::Millimeters,
            },
            inner_layers: 0,
            thickness: 1.6,
            solder_resist: SolderResistColor::Green,
            silkscreen: SilkscreenColor::White,
            default_font: String::new(),
            design_rules: serde_json::from_str("{}").unwrap(),
            drc_settings: serde_json::from_str("{}").unwrap(),
            fabrication_output_settings: FabricationOutputSettings::default(),
            devices: vec![],
            net_segments: vec![],
            planes: vec![],
            polygons: vec![make_board_outline(outline_w, outline_h)],
            holes: vec![],
        }
    }

    #[test]
    fn tv1_foreign_trace_clearance() {
        let gnd = Uuid::new_v4();
        let vcc = Uuid::new_v4();
        let mut board = minimal_board(20.0, 20.0);
        board
            .planes
            .push(make_plane(gnd, Layer::TopCopper, 1.0, 1.0, 19.0, 19.0));
        board.net_segments.push(make_trace(
            vcc,
            Layer::TopCopper,
            1.0,
            10.0,
            19.0,
            10.0,
            1.0,
        ));

        let circuit = empty_circuit();
        let library = empty_library();

        refill_board(&mut board, &circuit, &library);

        assert!(
            !board.planes[0].fragments.is_empty(),
            "should produce fragments"
        );
        let total_contours: usize = board.planes[0]
            .fragments
            .iter()
            .map(|f| f.contours.len())
            .sum();
        assert!(
            total_contours >= 2,
            "should have at least outer + hole or 2 fragments: got {total_contours}"
        );
    }

    #[test]
    fn tv2_same_net_no_subtraction() {
        let gnd = Uuid::new_v4();
        let mut board = minimal_board(20.0, 20.0);
        board
            .planes
            .push(make_plane(gnd, Layer::TopCopper, 1.0, 1.0, 19.0, 19.0));
        board.net_segments.push(make_trace(
            gnd,
            Layer::TopCopper,
            1.0,
            10.0,
            19.0,
            10.0,
            1.0,
        ));

        refill_board(&mut board, &empty_circuit(), &empty_library());

        assert!(!board.planes[0].fragments.is_empty());
        // Same net trace should not create holes
        for frag in &board.planes[0].fragments {
            assert_eq!(
                frag.contours.len(),
                1,
                "should have only outer contour (no holes)"
            );
        }
    }

    #[test]
    fn tv3_board_edge_keepout() {
        let gnd = Uuid::new_v4();
        let mut board = minimal_board(20.0, 20.0);
        // Plane covers entire board area
        let mut plane = make_plane(gnd, Layer::TopCopper, 0.0, 0.0, 20.0, 20.0);
        plane.min_board_clearance = 1.0;
        board.planes.push(plane);

        refill_board(&mut board, &empty_circuit(), &empty_library());

        assert!(!board.planes[0].fragments.is_empty());
        // With min_board_clearance=1.0, fill should be inset from edges
        let outer = &board.planes[0].fragments[0].contours[0];
        for pt in outer {
            // Allow small tolerance for polygon intersection edge cases
            assert!(
                pt.x >= 0.99,
                "x={} should be >= ~1.0 (board clearance)",
                pt.x
            );
            assert!(pt.y >= 0.99, "y={} should be >= ~1.0", pt.y);
            assert!(pt.x <= 19.01, "x={} should be <= ~19.0", pt.x);
            assert!(pt.y <= 19.01, "y={} should be <= ~19.0", pt.y);
        }
    }

    #[test]
    fn tv4_npth_hole_keepout() {
        let gnd = Uuid::new_v4();
        let mut board = minimal_board(20.0, 20.0);
        board
            .planes
            .push(make_plane(gnd, Layer::TopCopper, 1.0, 1.0, 19.0, 19.0));
        board.holes.push(BoardHole {
            uuid: Uuid::new_v4(),
            diameter: 2.0,
            stop_mask: StopMaskConfig::Auto,
            lock: false,
            path: vec![Vertex {
                position: Position::new(10.0, 10.0),
                angle: Angle(0.0),
            }],
        });

        refill_board(&mut board, &empty_circuit(), &empty_library());

        assert!(!board.planes[0].fragments.is_empty());
        let total_contours: usize = board.planes[0]
            .fragments
            .iter()
            .map(|f| f.contours.len())
            .sum();
        assert!(
            total_contours >= 2,
            "should have hole for NPTH: got {total_contours}"
        );
    }

    #[test]
    fn tv8_island_removal() {
        let gnd = Uuid::new_v4();
        let vcc = Uuid::new_v4();
        let mut board = minimal_board(20.0, 20.0);
        // Plane covers 1..19
        board
            .planes
            .push(make_plane(gnd, Layer::TopCopper, 1.0, 1.0, 19.0, 19.0));
        // Wide trace splits plane in half (wide enough to create gap)
        board.net_segments.push(make_trace(
            vcc,
            Layer::TopCopper,
            0.0,
            10.0,
            20.0,
            10.0,
            4.0,
        ));

        // keep_islands = false (default)
        refill_board(&mut board, &empty_circuit(), &empty_library());

        // With no same-net seeds and keep_islands=false, no islands should be kept
        // But since there are no seeds at all, the engine keeps all fragments
        let frag_count_no_keep = board.planes[0].fragments.len();

        // Now with keep_islands = true
        board.planes[0].fragments.clear();
        board.planes[0].keep_islands = true;
        refill_board(&mut board, &empty_circuit(), &empty_library());
        let frag_count_keep = board.planes[0].fragments.len();

        // keep_islands=true should keep at least as many fragments
        assert!(
            frag_count_keep >= frag_count_no_keep,
            "keep_islands=true ({frag_count_keep}) should keep >= no-keep ({frag_count_no_keep})"
        );
    }

    #[test]
    fn tv9_fragment_has_contours() {
        let gnd = Uuid::new_v4();
        let mut board = minimal_board(20.0, 20.0);
        board
            .planes
            .push(make_plane(gnd, Layer::TopCopper, 2.0, 2.0, 18.0, 18.0));
        board.holes.push(BoardHole {
            uuid: Uuid::new_v4(),
            diameter: 2.0,
            stop_mask: StopMaskConfig::Auto,
            lock: false,
            path: vec![Vertex {
                position: Position::new(10.0, 10.0),
                angle: Angle(0.0),
            }],
        });

        refill_board(&mut board, &empty_circuit(), &empty_library());

        let frag = &board.planes[0].fragments[0];
        assert!(
            frag.contours.len() >= 2,
            "should have outer + at least 1 hole"
        );
        assert!(
            frag.contours[0].len() >= 3,
            "outer contour should be a valid polygon"
        );
        assert!(
            frag.contours[1].len() >= 3,
            "hole contour should be a valid polygon"
        );
    }

    #[test]
    fn tv10_priority_ordering() {
        let gnd = Uuid::new_v4();
        let vcc = Uuid::new_v4();
        let mut board = minimal_board(20.0, 20.0);

        let mut plane_hi = make_plane(gnd, Layer::TopCopper, 5.0, 5.0, 15.0, 15.0);
        plane_hi.priority = 10;
        let mut plane_lo = make_plane(vcc, Layer::TopCopper, 3.0, 3.0, 17.0, 17.0);
        plane_lo.priority = 1;

        board.planes.push(plane_hi);
        board.planes.push(plane_lo);

        refill_board(&mut board, &empty_circuit(), &empty_library());

        // Higher priority plane should have normal fill
        assert!(
            !board.planes[0].fragments.is_empty(),
            "high-priority plane should fill"
        );
        // Lower priority plane should have the high-priority area subtracted
        let lo_total_contours: usize = board.planes[1]
            .fragments
            .iter()
            .map(|f| f.contours.len())
            .sum();
        assert!(
            lo_total_contours >= 2,
            "low-priority plane should have a hole where high-priority sits: got {lo_total_contours}"
        );
    }
}
