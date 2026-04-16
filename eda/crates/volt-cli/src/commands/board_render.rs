//! Board (PCB) SVG renderer.
//!
//! Generates an SVG image from a board layout for visual feedback.
//! Renders board outline, copper planes, traces, footprint pads, vias, holes,
//! silkscreen, and component designators.

use std::collections::HashMap;
use std::fmt::Write;
use std::path::Path;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Device, FootprintPad, Footprint, Package};
use volt_core::project::*;

use super::project_io::{self, Result};

// ===========================================================================
// Layer color scheme
// ===========================================================================

fn layer_color(layer: &Layer) -> &'static str {
    match layer {
        Layer::TopCopper => "#cc0000",
        Layer::BottomCopper => "#0000cc",
        Layer::TopLegend => "#ffff00",
        Layer::BottomLegend => "#00cccc",
        Layer::TopCourtyard => "#ff00ff",
        Layer::BoardOutlines => "#ffff00",
        _ => "#888888",
    }
}

fn pad_fill(side: PadSide) -> &'static str {
    match side {
        PadSide::Top => "#cc0000",
        PadSide::Bottom => "#0000cc",
        PadSide::ThroughHole => "#00aa00",
    }
}

const BG_COLOR: &str = "#1a4d1a";
const VIA_FILL: &str = "#008800";
const VIA_DRILL: &str = "#ffffff";
const HOLE_COLOR: &str = "#ffffff";
const GRID_DOT_COLOR: &str = "rgba(255,255,255,0.08)";

// ===========================================================================
// Public entry point
// ===========================================================================

/// Render a board to an SVG file.
pub fn render_board(project: &Path, board_name: &str, output: &Path) -> Result<()> {
    project_io::ensure_project(project)?;

    let board = project_io::read_board(project, board_name)?;
    let circuit = project_io::read_circuit(project)?;

    // Build component-UUID → designator map
    let comp_name: HashMap<Uuid, &str> = circuit
        .components
        .iter()
        .map(|c| (c.uuid, c.name.as_str()))
        .collect();

    // Load packages & footprints for all board devices
    let mut pkg_cache: HashMap<Uuid, Package> = HashMap::new();
    let mut dev_cache: HashMap<Uuid, Device> = HashMap::new();

    for bd in &board.devices {
        if !dev_cache.contains_key(&bd.lib_device) {
            if let Ok(dev) =
                project_io::read_library_element::<Device>(project, "devices", &bd.lib_device)
            {
                if !pkg_cache.contains_key(&dev.package) {
                    if let Ok(pkg) =
                        project_io::read_library_element::<Package>(project, "packages", &dev.package)
                    {
                        pkg_cache.insert(dev.package, pkg);
                    }
                }
                dev_cache.insert(bd.lib_device, dev);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Bounding box (from board outline, falling back to all elements)
    // -----------------------------------------------------------------------
    let (min_x, min_y, max_x, max_y) = board_bounds(&board, &dev_cache, &pkg_cache);
    let margin = 5.0; // mm
    let vx = min_x - margin;
    let vy = min_y - margin;
    let vw = (max_x - min_x) + margin * 2.0;
    let vh = (max_y - min_y) + margin * 2.0;

    // Scale: 10 px per mm gives nice resolution
    let px_w = (vw * 10.0) as i32;
    let px_h = (vh * 10.0) as i32;

    let mut svg = String::with_capacity(16384);

    // SVG header
    writeln!(
        svg,
        r#"<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vx:.3} {vy:.3} {vw:.3} {vh:.3}" width="{px_w}" height="{px_h}">"#
    )?;

    // CSS styles
    write_styles(&mut svg)?;

    // (a) Background
    writeln!(
        svg,
        r#"<rect x="{vx:.3}" y="{vy:.3}" width="{vw:.3}" height="{vh:.3}" class="bg"/>"#
    )?;

    // Grid dots (subtle)
    write_grid(&mut svg, vx, vy, vw, vh, &board.grid)?;

    // (b) Board outline
    for poly in &board.polygons {
        if poly.layer == Layer::BoardOutlines {
            write_polygon_outline(&mut svg, &poly.vertices, "outline")?;
        }
    }

    // (c) Copper planes (semi-transparent)
    for plane in &board.planes {
        write_filled_polygon(&mut svg, &plane.vertices, layer_color(&plane.layer), 0.30)?;
    }

    // (d) Traces
    for seg in &board.net_segments {
        for trace in &seg.traces {
            let from = resolve_trace_endpoint(&trace.from, &board, &seg.junctions, project, &dev_cache, &pkg_cache);
            let to = resolve_trace_endpoint(&trace.to, &board, &seg.junctions, project, &dev_cache, &pkg_cache);
            if let (Some(fp), Some(tp)) = (from, to) {
                writeln!(
                    svg,
                    r#"<line x1="{:.3}" y1="{:.3}" x2="{:.3}" y2="{:.3}" stroke="{}" stroke-width="{:.3}" stroke-linecap="round" class="trace"/>"#,
                    fp.0, fp.1, tp.0, tp.1,
                    layer_color(&trace.layer),
                    trace.width,
                )?;
            }
        }
    }

    // (e) Footprint pads
    for bd in &board.devices {
        if let Some(footprint) = get_footprint(bd, &dev_cache, &pkg_cache) {
            for fp_pad in &footprint.pads {
                write_pad(&mut svg, fp_pad, bd)?;
            }
        }
    }

    // (f) Vias
    for seg in &board.net_segments {
        for via in &seg.vias {
            let outer_r = via_outer_radius(via, &board.design_rules);
            writeln!(
                svg,
                r#"<circle cx="{:.3}" cy="{:.3}" r="{:.3}" fill="{VIA_FILL}" class="via"/>"#,
                via.position.x, via.position.y, outer_r,
            )?;
            let drill_r = via.drill / 2.0;
            writeln!(
                svg,
                r#"<circle cx="{:.3}" cy="{:.3}" r="{:.3}" fill="{VIA_DRILL}" class="via-drill"/>"#,
                via.position.x, via.position.y, drill_r,
            )?;
        }
    }

    // (g) Non-plated holes
    for hole in &board.holes {
        if let Some(v) = hole.path.first() {
            let r = hole.diameter / 2.0;
            writeln!(
                svg,
                r#"<circle cx="{:.3}" cy="{:.3}" r="{:.3}" fill="{HOLE_COLOR}" class="hole"/>"#,
                v.position.x, v.position.y, r,
            )?;
        }
    }

    // (h) Silkscreen (footprint polygons on legend layers)
    for bd in &board.devices {
        if let Some(footprint) = get_footprint(bd, &dev_cache, &pkg_cache) {
            for poly in &footprint.polygons {
                if poly.layer == Layer::TopLegend || poly.layer == Layer::BottomLegend {
                    let color = layer_color(&poly.layer);
                    let transformed = transform_vertices(&poly.vertices, bd);
                    write_polygon_stroke(&mut svg, &transformed, color, poly.width.max(0.15))?;
                }
            }
        }
    }

    // (i) Component designators
    for bd in &board.devices {
        let name = comp_name
            .get(&bd.component)
            .copied()
            .unwrap_or("?");
        let color = if bd.flip { "#00cccc" } else { "#ffff00" };
        // Place text slightly above the device position
        let ty = bd.position.y - 2.5;
        writeln!(
            svg,
            r#"<text x="{:.3}" y="{:.3}" fill="{color}" class="designator">{}</text>"#,
            bd.position.x, ty, escape_xml(name),
        )?;
    }

    writeln!(svg, "</svg>")?;

    std::fs::write(output, &svg)?;

    // Summary JSON
    let result = serde_json::json!({
        "status": "ok",
        "output": output.display().to_string(),
        "devices": board.devices.len(),
        "traces": board.net_segments.iter().map(|s| s.traces.len()).sum::<usize>(),
        "planes": board.planes.len(),
        "holes": board.holes.len(),
        "vias": board.net_segments.iter().map(|s| s.vias.len()).sum::<usize>(),
        "size": { "width": vw, "height": vh },
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

// ===========================================================================
// SVG helpers
// ===========================================================================

fn write_styles(svg: &mut String) -> std::fmt::Result {
    writeln!(svg, r#"<style>
  .bg {{ fill: {BG_COLOR}; }}
  .outline {{ stroke: #c0c0c0; stroke-width: 0.2; fill: none; }}
  .trace {{ fill: none; }}
  .via {{ }}
  .via-drill {{ }}
  .hole {{ }}
  .designator {{
    font-family: 'Helvetica Neue', Arial, sans-serif;
    font-size: 1.8px;
    text-anchor: middle;
    dominant-baseline: auto;
    font-weight: bold;
  }}
  .pad {{ }}
  .pad-drill {{ fill: {BG_COLOR}; }}
  .grid-dot {{ fill: {GRID_DOT_COLOR}; }}
</style>"#)
}

fn write_grid(
    svg: &mut String,
    vx: f64,
    vy: f64,
    vw: f64,
    vh: f64,
    grid: &Grid,
) -> std::fmt::Result {
    let interval = if grid.interval > 0.0 { grid.interval } else { 1.0 };
    // Only draw grid if it won't be absurdly dense
    let dots_x = (vw / interval) as i32;
    let dots_y = (vh / interval) as i32;
    if dots_x * dots_y > 10000 {
        return Ok(());
    }

    let gx_start = (vx / interval).ceil() as i32;
    let gx_end = ((vx + vw) / interval).floor() as i32;
    let gy_start = (vy / interval).ceil() as i32;
    let gy_end = ((vy + vh) / interval).floor() as i32;

    for gx in gx_start..=gx_end {
        for gy in gy_start..=gy_end {
            let x = gx as f64 * interval;
            let y = gy as f64 * interval;
            writeln!(
                svg,
                r#"<circle cx="{x:.2}" cy="{y:.2}" r="0.15" class="grid-dot"/>"#
            )?;
        }
    }
    Ok(())
}

fn write_polygon_outline(
    svg: &mut String,
    vertices: &[Vertex],
    class: &str,
) -> std::fmt::Result {
    if vertices.len() < 2 {
        return Ok(());
    }
    write!(svg, r#"<polyline points=""#)?;
    for (i, v) in vertices.iter().enumerate() {
        if i > 0 {
            write!(svg, " ")?;
        }
        write!(svg, "{:.3},{:.3}", v.position.x, v.position.y)?;
    }
    writeln!(svg, r#"" class="{class}"/>"#)
}

fn write_filled_polygon(
    svg: &mut String,
    vertices: &[Vertex],
    color: &str,
    opacity: f64,
) -> std::fmt::Result {
    if vertices.len() < 3 {
        return Ok(());
    }
    write!(svg, r#"<polygon points=""#)?;
    for (i, v) in vertices.iter().enumerate() {
        if i > 0 {
            write!(svg, " ")?;
        }
        write!(svg, "{:.3},{:.3}", v.position.x, v.position.y)?;
    }
    writeln!(
        svg,
        r#"" fill="{color}" fill-opacity="{opacity:.2}" stroke="{color}" stroke-width="0.1" stroke-opacity="{:.2}"/>"#,
        (opacity + 0.1).min(1.0),
    )
}

fn write_polygon_stroke(
    svg: &mut String,
    vertices: &[(f64, f64)],
    color: &str,
    width: f64,
) -> std::fmt::Result {
    if vertices.len() < 2 {
        return Ok(());
    }
    write!(svg, r#"<polyline points=""#)?;
    for (i, (x, y)) in vertices.iter().enumerate() {
        if i > 0 {
            write!(svg, " ")?;
        }
        write!(svg, "{x:.3},{y:.3}")?;
    }
    writeln!(
        svg,
        r#"" stroke="{color}" stroke-width="{width:.3}" fill="none"/>"#
    )
}

fn write_pad(
    svg: &mut String,
    fp_pad: &FootprintPad,
    bd: &BoardDevice,
) -> std::fmt::Result {
    let (wx, wy) = transform_point(fp_pad.position.x, fp_pad.position.y, bd);
    let total_rot = if bd.flip {
        -fp_pad.rotation.0 + bd.rotation.0
    } else {
        fp_pad.rotation.0 + bd.rotation.0
    };
    let fill = pad_fill(if bd.flip && fp_pad.side == PadSide::Top {
        PadSide::Bottom
    } else if bd.flip && fp_pad.side == PadSide::Bottom {
        PadSide::Top
    } else {
        fp_pad.side
    });

    let w = fp_pad.width;
    let h = fp_pad.height;

    match fp_pad.shape {
        PadShape::Round => {
            let r = w.max(h) / 2.0;
            writeln!(
                svg,
                r#"<circle cx="{wx:.3}" cy="{wy:.3}" r="{r:.3}" fill="{fill}" class="pad"/>"#,
            )?;
        }
        PadShape::RoundRect | PadShape::Custom => {
            let rx = fp_pad.radius * w.min(h) / 2.0;
            // rect origin is top-left
            let half_w = w / 2.0;
            let half_h = h / 2.0;
            if total_rot.abs() > 0.01 {
                writeln!(
                    svg,
                    r#"<rect x="{:.3}" y="{:.3}" width="{w:.3}" height="{h:.3}" rx="{rx:.3}" ry="{rx:.3}" fill="{fill}" transform="translate({wx:.3},{wy:.3}) rotate({total_rot:.2}) translate({:.3},{:.3})" class="pad"/>"#,
                    0.0, 0.0, -half_w, -half_h,
                )?;
            } else {
                writeln!(
                    svg,
                    r#"<rect x="{:.3}" y="{:.3}" width="{w:.3}" height="{h:.3}" rx="{rx:.3}" ry="{rx:.3}" fill="{fill}" class="pad"/>"#,
                    wx - half_w, wy - half_h,
                )?;
            }
        }
    }

    // Draw drill holes for THT pads
    for hole in &fp_pad.holes {
        let drill_r = hole.diameter / 2.0;
        writeln!(
            svg,
            r#"<circle cx="{wx:.3}" cy="{wy:.3}" r="{drill_r:.3}" class="pad-drill"/>"#,
        )?;
    }

    Ok(())
}

// ===========================================================================
// Coordinate transforms
// ===========================================================================

/// Transform a footprint-local point to board world coordinates.
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

/// Transform a list of footprint-local vertices to world coordinates.
fn transform_vertices(vertices: &[Vertex], bd: &BoardDevice) -> Vec<(f64, f64)> {
    vertices
        .iter()
        .map(|v| transform_point(v.position.x, v.position.y, bd))
        .collect()
}

// ===========================================================================
// Trace endpoint resolution
// ===========================================================================

fn resolve_trace_endpoint(
    ep: &TraceEndpoint,
    board: &Board,
    local_junctions: &[Junction],
    _project: &Path,    dev_cache: &HashMap<Uuid, Device>,
    pkg_cache: &HashMap<Uuid, Package>,
) -> Option<(f64, f64)> {
    match ep {
        TraceEndpoint::Device { device, pad } => {
            // Find the BoardDevice
            let bd = board.devices.iter().find(|d| d.component == *device)?;
            let footprint = get_footprint(bd, dev_cache, pkg_cache)?;
            let fp_pad = footprint.pads.iter().find(|p| p.uuid == *pad)?;
            let (wx, wy) = transform_point(fp_pad.position.x, fp_pad.position.y, bd);
            Some((wx, wy))
        }
        TraceEndpoint::Junction { junction } => {
            // Search local junctions first, then all net segments
            if let Some(j) = local_junctions.iter().find(|j| j.uuid == *junction) {
                return Some((j.position.x, j.position.y));
            }
            for seg in &board.net_segments {
                if let Some(j) = seg.junctions.iter().find(|j| j.uuid == *junction) {
                    return Some((j.position.x, j.position.y));
                }
            }
            None
        }
        TraceEndpoint::Via { via } => {
            for seg in &board.net_segments {
                if let Some(v) = seg.vias.iter().find(|v| v.uuid == *via) {
                    return Some((v.position.x, v.position.y));
                }
            }
            None
        }
    }
}

// ===========================================================================
// Footprint lookup
// ===========================================================================

fn get_footprint<'a>(
    bd: &BoardDevice,
    dev_cache: &HashMap<Uuid, Device>,
    pkg_cache: &'a HashMap<Uuid, Package>,
) -> Option<&'a Footprint> {
    let dev = dev_cache.get(&bd.lib_device)?;
    let pkg = pkg_cache.get(&dev.package)?;
    pkg.footprints
        .iter()
        .find(|f| f.uuid == bd.lib_footprint)
        .or_else(|| pkg.footprints.first())
}

// ===========================================================================
// Bounding box
// ===========================================================================

fn board_bounds(
    board: &Board,
    dev_cache: &HashMap<Uuid, Device>,
    pkg_cache: &HashMap<Uuid, Package>,
) -> (f64, f64, f64, f64) {
    let mut min_x = f64::INFINITY;
    let mut min_y = f64::INFINITY;
    let mut max_x = f64::NEG_INFINITY;
    let mut max_y = f64::NEG_INFINITY;

    let mut expand = |x: f64, y: f64| {
        min_x = min_x.min(x);
        min_y = min_y.min(y);
        max_x = max_x.max(x);
        max_y = max_y.max(y);
    };

    // Board outline polygons
    let mut has_outline = false;
    for poly in &board.polygons {
        if poly.layer == Layer::BoardOutlines {
            for v in &poly.vertices {
                expand(v.position.x, v.position.y);
                has_outline = true;
            }
        }
    }

    if has_outline {
        return (min_x, min_y, max_x, max_y);
    }

    // Fallback: include all elements
    for bd in &board.devices {
        expand(bd.position.x - 5.0, bd.position.y - 5.0);
        expand(bd.position.x + 5.0, bd.position.y + 5.0);

        if let Some(fp) = get_footprint(bd, dev_cache, pkg_cache) {
            for pad in &fp.pads {
                let (wx, wy) = transform_point(pad.position.x, pad.position.y, bd);
                let half = pad.width.max(pad.height) / 2.0;
                expand(wx - half, wy - half);
                expand(wx + half, wy + half);
            }
        }
    }

    for seg in &board.net_segments {
        for j in &seg.junctions {
            expand(j.position.x, j.position.y);
        }
        for via in &seg.vias {
            expand(via.position.x - 1.0, via.position.y - 1.0);
            expand(via.position.x + 1.0, via.position.y + 1.0);
        }
    }

    for plane in &board.planes {
        for v in &plane.vertices {
            expand(v.position.x, v.position.y);
        }
    }

    for hole in &board.holes {
        for v in &hole.path {
            let r = hole.diameter / 2.0;
            expand(v.position.x - r, v.position.y - r);
            expand(v.position.x + r, v.position.y + r);
        }
    }

    if min_x == f64::INFINITY {
        (0.0, 0.0, 100.0, 100.0)
    } else {
        (min_x, min_y, max_x, max_y)
    }
}

// ===========================================================================
// Misc helpers
// ===========================================================================

fn via_outer_radius(via: &Via, rules: &DesignRules) -> f64 {
    match via.size {
        ViaSize::Manual(s) => s / 2.0,
        ViaSize::Auto => {
            // Drill + annular ring
            let ring = rules.via_annular_ring_min.max(0.2);
            (via.drill / 2.0) + ring
        }
    }
}

fn escape_xml(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
}
