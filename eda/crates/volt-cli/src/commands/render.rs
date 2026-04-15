//! Schematic SVG renderer.
//!
//! Generates an SVG image from a schematic for agent visual feedback.
//! The agent can inspect this to verify symbol placement, wiring, and layout.

use std::collections::HashMap;
use std::fmt::Write;
use std::path::Path;

use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Component, Symbol, SymbolPin};
use volt_core::project::*;

use super::project_io::{self, Result};

/// Render a schematic to SVG file.
pub fn render_schematic(
    project: &Path,
    sch_name: &str,
    output: &Path,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let schematic = project_io::read_schematic(project, sch_name)?;

    // Build lookup maps
    let comp_map: HashMap<Uuid, &ComponentInstance> = circuit.components.iter()
        .map(|c| (c.uuid, c))
        .collect();

    let net_map: HashMap<Uuid, &Net> = circuit.nets.iter()
        .map(|n| (n.uuid, n))
        .collect();

    // Load all referenced symbols and components
    let mut sym_cache: HashMap<Uuid, Symbol> = HashMap::new();
    let mut comp_cache: HashMap<Uuid, Component> = HashMap::new();

    for sch_sym in &schematic.symbols {
        if let Some(comp_inst) = comp_map.get(&sch_sym.component) {
            if !comp_cache.contains_key(&comp_inst.lib_component) {
                if let Ok(lib_comp) = project_io::read_library_element::<Component>(
                    project, "components", &comp_inst.lib_component,
                ) {
                    // Find the gate's symbol UUID
                    if let Some(variant) = lib_comp.variants.iter().find(|v| v.uuid == comp_inst.lib_variant) {
                        if let Some(gate) = variant.gates.iter().find(|g| g.uuid == sch_sym.lib_gate) {
                            if !sym_cache.contains_key(&gate.symbol) {
                                if let Ok(sym) = project_io::read_library_element::<Symbol>(
                                    project, "symbols", &gate.symbol,
                                ) {
                                    sym_cache.insert(gate.symbol, sym);
                                }
                            }
                        }
                    }
                    comp_cache.insert(comp_inst.lib_component, lib_comp);
                }
            }
        }
    }

    // Calculate bounding box
    let (min_x, min_y, max_x, max_y) = calculate_bounds(&schematic, &comp_map, &comp_cache, &sym_cache);
    let margin = 20.0;
    let vx = min_x - margin;
    let vy = min_y - margin;
    let vw = (max_x - min_x) + margin * 2.0;
    let vh = (max_y - min_y) + margin * 2.0;

    let mut svg = String::with_capacity(8192);

    // SVG header
    writeln!(svg, r#"<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vx:.2} {vy:.2} {vw:.2} {vh:.2}" width="{}" height="{}">"#,
        (vw * 4.0) as i32, (vh * 4.0) as i32)?;

    // Style
    writeln!(svg, r#"<style>
  .wire {{ stroke: #007030; stroke-width: 0.3; fill: none; }}
  .outline {{ stroke: #800000; stroke-width: 0.254; fill: none; }}
  .pin-line {{ stroke: #006060; stroke-width: 0.15; }}
  .pin-dot {{ fill: #007030; }}
  .junction {{ fill: #007030; }}
  .text-name {{ font-family: sans-serif; font-size: 2.2px; fill: #800000; }}
  .text-value {{ font-family: sans-serif; font-size: 2.2px; fill: #006060; }}
  .text-pin-number {{ font-family: sans-serif; font-size: 1.6px; fill: #006060; }}
  .text-pin-name {{ font-family: sans-serif; font-size: 1.7px; fill: #005050; }}
  .text-label {{ font-family: sans-serif; font-size: 2.5px; fill: #007030; font-weight: bold; }}
  .grid-dot {{ fill: #e0e0e0; }}
</style>"#)?;

    // Background
    writeln!(svg, r#"<rect x="{vx:.2}" y="{vy:.2}" width="{vw:.2}" height="{vh:.2}" fill="white"/>"#)?;

    // Grid dots
    let grid_interval = schematic.grid.interval;
    if grid_interval > 0.0 {
        let gx_start = (vx / grid_interval).ceil() as i32;
        let gx_end = ((vx + vw) / grid_interval).floor() as i32;
        let gy_start = (vy / grid_interval).ceil() as i32;
        let gy_end = ((vy + vh) / grid_interval).floor() as i32;

        for gx in gx_start..=gx_end {
            for gy in gy_start..=gy_end {
                let x = gx as f64 * grid_interval;
                let y = gy as f64 * grid_interval;
                writeln!(svg, r#"<circle cx="{x:.2}" cy="{y:.2}" r="0.3" class="grid-dot"/>"#)?;
            }
        }
    }

    // Render symbols
    for sch_sym in &schematic.symbols {
        let comp_inst = match comp_map.get(&sch_sym.component) {
            Some(c) => c,
            None => continue,
        };
        let lib_comp = match comp_cache.get(&comp_inst.lib_component) {
            Some(c) => c,
            None => continue,
        };
        let variant = match lib_comp.variants.iter().find(|v| v.uuid == comp_inst.lib_variant) {
            Some(v) => v,
            None => continue,
        };
        let gate = match variant.gates.iter().find(|g| g.uuid == sch_sym.lib_gate) {
            Some(g) => g,
            None => continue,
        };
        let lib_sym = match sym_cache.get(&gate.symbol) {
            Some(s) => s,
            None => continue,
        };

        render_symbol(&mut svg, sch_sym, lib_sym, &comp_inst.name, &comp_inst.value)?;
    }

    // Render net segments (wires, junctions, labels)
    for seg in &schematic.net_segments {
        let net_name = net_map.get(&seg.net).map(|n| n.name.as_str()).unwrap_or("?");

        // Render wires
        for line in &seg.lines {
            let from_pos = resolve_endpoint_pos(&line.from, &schematic, &comp_map, &comp_cache, &sym_cache);
            let to_pos = resolve_endpoint_pos(&line.to, &schematic, &comp_map, &comp_cache, &sym_cache);

            if let (Some(fp), Some(tp)) = (from_pos, to_pos) {
                writeln!(svg, r#"<line x1="{:.2}" y1="{:.2}" x2="{:.2}" y2="{:.2}" class="wire"/>"#,
                    fp.x, fp.y, tp.x, tp.y)?;
            }
        }

        // Render junctions
        for junc in &seg.junctions {
            writeln!(svg, r#"<circle cx="{:.2}" cy="{:.2}" r="0.6" class="junction"/>"#,
                junc.position.x, junc.position.y)?;
        }

        // Render labels
        for label in &seg.labels {
            write_svg_text(
                &mut svg,
                "text-label",
                Position::new(label.position.x, label.position.y - 0.5),
                label.rotation,
                Alignment { h: HAlign::Left, v: VAlign::Bottom },
                net_name,
            )?;
        }
    }

    writeln!(svg, "</svg>")?;

    std::fs::write(output, &svg)?;

    let result = serde_json::json!({
        "status": "ok",
        "output": output.display().to_string(),
        "symbols": schematic.symbols.len(),
        "net_segments": schematic.net_segments.len(),
        "size": { "width": vw, "height": vh },
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn render_symbol(
    svg: &mut String,
    sch_sym: &SchematicSymbol,
    lib_sym: &Symbol,
    name: &str,
    value: &str,
) -> std::fmt::Result {
    let px = sch_sym.position.x;
    let py = sch_sym.position.y;
    let rot = sch_sym.rotation.0;

    writeln!(svg, r#"<g transform="translate({px:.2},{py:.2}) rotate({rot:.1})">"#)?;

    // Draw polygons (outlines)
    for poly in &lib_sym.polygons {
        if poly.vertices.len() < 2 {
            continue;
        }
        write!(svg, r#"<polyline points=""#)?;
        for (i, v) in poly.vertices.iter().enumerate() {
            if i > 0 { write!(svg, " ")?; }
            write!(svg, "{:.2},{:.2}", v.position.x, v.position.y)?;
        }
        writeln!(svg, r#"" class="outline"/>"#)?;
    }

    // Draw pins
    for pin in &lib_sym.pins {
        let pin_end = pin_endpoint(pin);
        // Pin line from connection point to body
        writeln!(svg, r#"<line x1="{:.2}" y1="{:.2}" x2="{:.2}" y2="{:.2}" class="pin-line"/>"#,
            pin.position.x, pin.position.y, pin_end.x, pin_end.y)?;
        // Pin dot at connection point
        writeln!(svg, r#"<circle cx="{:.2}" cy="{:.2}" r="0.4" class="pin-dot"/>"#,
            pin.position.x, pin.position.y)?;

        let (number_pos, number_align) = pin_number_position(pin);
        write_svg_text(svg, "text-pin-number", number_pos, Angle(0.0), number_align, &pin.name)?;

        if !pin.pin_name.is_empty() {
            write_svg_text(svg, "text-pin-name", pin_name_position(pin), Angle(0.0), pin.name_align, &pin.pin_name)?;
        }
    }

    writeln!(svg, "</g>")?;

    // Draw name and value texts (in world space, not rotated with symbol)
    for text in &sch_sym.texts {
        let css_class = if text.value.contains("NAME") { "text-name" } else { "text-value" };
        let display_value = text.value
            .replace("{{NAME}}", name)
            .replace("{{VALUE}}", value);
        write_svg_text(svg, css_class, text.position, text.rotation, text.align, &display_value)?;
    }

    Ok(())
}

fn write_svg_text(
    svg: &mut String,
    class_name: &str,
    position: Position,
    rotation: Angle,
    align: Alignment,
    value: &str,
) -> std::fmt::Result {
    let anchor = svg_text_anchor(align.h);
    let baseline = svg_dominant_baseline(align.v);
    let escaped = escape_xml_text(value);

    if rotation.0.abs() > 0.01 {
        writeln!(
            svg,
            r#"<text x="{:.2}" y="{:.2}" class="{}" text-anchor="{}" dominant-baseline="{}" transform="rotate({:.1},{:.2},{:.2})">{}</text>"#,
            position.x,
            position.y,
            class_name,
            anchor,
            baseline,
            rotation.0,
            position.x,
            position.y,
            escaped,
        )
    } else {
        writeln!(
            svg,
            r#"<text x="{:.2}" y="{:.2}" class="{}" text-anchor="{}" dominant-baseline="{}">{}</text>"#,
            position.x,
            position.y,
            class_name,
            anchor,
            baseline,
            escaped,
        )
    }
}

fn svg_text_anchor(align: HAlign) -> &'static str {
    match align {
        HAlign::Left => "start",
        HAlign::Center => "middle",
        HAlign::Right => "end",
    }
}

fn svg_dominant_baseline(align: VAlign) -> &'static str {
    match align {
        VAlign::Top => "hanging",
        VAlign::Center => "middle",
        VAlign::Bottom => "text-after-edge",
    }
}

fn escape_xml_text(value: &str) -> String {
    value
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
}

fn pin_number_position(pin: &SymbolPin) -> (Position, Alignment) {
    match normalize_angle(pin.rotation.0) as i32 {
        0 => (
            Position::new(pin.position.x - 0.6, pin.position.y - 0.8),
            Alignment { h: HAlign::Right, v: VAlign::Bottom },
        ),
        90 => (
            Position::new(pin.position.x + 0.8, pin.position.y - 0.4),
            Alignment { h: HAlign::Left, v: VAlign::Bottom },
        ),
        180 => (
            Position::new(pin.position.x + 0.6, pin.position.y - 0.8),
            Alignment { h: HAlign::Left, v: VAlign::Bottom },
        ),
        270 => (
            Position::new(pin.position.x + 0.8, pin.position.y + 0.4),
            Alignment { h: HAlign::Left, v: VAlign::Top },
        ),
        _ => (
            Position::new(pin.position.x, pin.position.y - 0.8),
            Alignment { h: HAlign::Center, v: VAlign::Bottom },
        ),
    }
}

fn pin_name_position(pin: &SymbolPin) -> Position {
    if pin.name_position != Position::default() {
        return pin.name_position;
    }

    let end = pin_endpoint(pin);
    match normalize_angle(pin.rotation.0) as i32 {
        0 => Position::new(end.x + 0.6, end.y),
        90 => Position::new(end.x + 0.6, end.y + 0.4),
        180 => Position::new(end.x - 0.6, end.y),
        270 => Position::new(end.x + 0.6, end.y - 0.4),
        _ => end,
    }
}

fn normalize_angle(angle: f64) -> f64 {
    let mut angle = angle % 360.0;
    if angle < 0.0 {
        angle += 360.0;
    }
    angle.round()
}

/// Calculate the endpoint position of a pin (where it meets the symbol body).
fn pin_endpoint(pin: &SymbolPin) -> Position {
    let angle_rad = pin.rotation.0.to_radians();
    Position::new(
        pin.position.x + pin.length * angle_rad.cos(),
        pin.position.y + pin.length * angle_rad.sin(),
    )
}

/// Resolve a LineEndpoint to a world position.
fn resolve_endpoint_pos(
    ep: &LineEndpoint,
    schematic: &Schematic,
    comp_map: &HashMap<Uuid, &ComponentInstance>,
    comp_cache: &HashMap<Uuid, Component>,
    sym_cache: &HashMap<Uuid, Symbol>,
) -> Option<Position> {
    match ep {
        LineEndpoint::Junction { junction } => {
            for seg in &schematic.net_segments {
                if let Some(j) = seg.junctions.iter().find(|j| j.uuid == *junction) {
                    return Some(j.position);
                }
            }
            None
        }
        LineEndpoint::Symbol { symbol, pin } => {
            let sch_sym = schematic.symbols.iter().find(|s| s.uuid == *symbol)?;
            let comp_inst = comp_map.get(&sch_sym.component)?;
            let lib_comp = comp_cache.get(&comp_inst.lib_component)?;
            let variant = lib_comp.variants.iter().find(|v| v.uuid == comp_inst.lib_variant)?;
            let gate = variant.gates.iter().find(|g| g.uuid == sch_sym.lib_gate)?;
            let lib_sym = sym_cache.get(&gate.symbol)?;
            let lib_pin = lib_sym.pins.iter().find(|p| p.uuid == *pin)?;

            // Transform pin position by symbol's position and rotation
            let rot_rad = sch_sym.rotation.0.to_radians();
            let cos_r = rot_rad.cos();
            let sin_r = rot_rad.sin();
            let px = lib_pin.position.x;
            let py = lib_pin.position.y;

            Some(Position::new(
                sch_sym.position.x + px * cos_r - py * sin_r,
                sch_sym.position.y + px * sin_r + py * cos_r,
            ))
        }
    }
}

fn calculate_bounds(
    schematic: &Schematic,
    _comp_map: &HashMap<Uuid, &ComponentInstance>,
    _comp_cache: &HashMap<Uuid, Component>,
    _sym_cache: &HashMap<Uuid, Symbol>,
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

    for sch_sym in &schematic.symbols {
        expand(sch_sym.position.x - 10.0, sch_sym.position.y - 10.0);
        expand(sch_sym.position.x + 10.0, sch_sym.position.y + 10.0);
    }

    for seg in &schematic.net_segments {
        for junc in &seg.junctions {
            expand(junc.position.x, junc.position.y);
        }
        for label in &seg.labels {
            expand(label.position.x, label.position.y);
        }
    }

    if min_x == f64::INFINITY {
        // Empty schematic
        (0.0, 0.0, 100.0, 100.0)
    } else {
        (min_x, min_y, max_x, max_y)
    }
}
