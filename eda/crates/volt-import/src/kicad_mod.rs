//! KiCad `.kicad_mod` footprint parser.
//!
//! Parses KiCad 6+ footprint files and converts them to `volt-core` types.
//!
//! KiCad footprint format structure:
//! - `(footprint "NAME" ...)` or `(module "NAME" ...)` for older files
//! - `(pad "1" smd rect (at x y) (size w h) (layers ...))` — pads
//! - `(fp_line (start x y) (end x y) (layer ...) (stroke ...))` — lines
//! - `(fp_rect (start x y) (end x y) (layer ...) (stroke ...))` — rectangles
//! - `(fp_poly (pts ...) (layer ...) (stroke ...))` — polygons
//! - `(fp_text reference "REF**" (at x y) (layer ...) ...)` — text

use std::collections::HashMap;
use std::path::Path;

use volt_core::common::*;
use volt_core::library::*;

use crate::sexp::{parse, SExpr};

/// Parse a `.kicad_mod` file from disk and return a [`Package`].
pub fn parse_kicad_mod_file(path: &Path) -> Result<Package, String> {
    let content = std::fs::read_to_string(path)
        .map_err(|e| format!("Failed to read {}: {e}", path.display()))?;
    parse_kicad_mod(&content)
}

/// Parse a `.kicad_mod` string and return a [`Package`].
pub fn parse_kicad_mod(content: &str) -> Result<Package, String> {
    let root = parse(content).map_err(|e| format!("S-expr parse error: {e}"))?;

    // Root should be (footprint ...) or (module ...) for older files
    match root.keyword() {
        Some("footprint") | Some("module") => {}
        _ => return Err("Expected footprint or module root element".into()),
    }

    parse_footprint_node(&root)
}

fn parse_footprint_node(node: &SExpr) -> Result<Package, String> {
    let children = node.children();
    if children.len() < 2 {
        return Err("Footprint node too short".into());
    }

    // First arg is the footprint name
    let fp_name = children
        .get(1)
        .and_then(|c| c.as_str().or_else(|| c.as_atom()))
        .ok_or("Missing footprint name")?
        .to_string();

    // Extract metadata
    let description = get_property(node, "descr")
        .or_else(|| get_property(node, "description"))
        .or_else(|| get_property(node, "Description"))
        .unwrap_or_default();
    let keywords = get_property(node, "tags").unwrap_or_default();

    let now = chrono::Utc::now();
    let pkg_uuid = new_uuid();

    // Parse pads
    let mut footprint_pads = Vec::new();
    let mut package_pad_map: HashMap<String, uuid::Uuid> = HashMap::new();
    let mut has_tht = false;
    let mut has_smd = false;

    for child in node.children() {
        if child.keyword() != Some("pad") {
            continue;
        }
        if let Some((fp_pad, pad_name, is_tht)) = parse_pad(child, &mut package_pad_map) {
            footprint_pads.push(fp_pad);
            if is_tht {
                has_tht = true;
            } else {
                has_smd = true;
            }
            let _ = pad_name; // already inserted into map
        }
    }

    // Build PackagePad list from the map (sorted by name for determinism)
    let mut pad_entries: Vec<(String, uuid::Uuid)> = package_pad_map.into_iter().collect();
    pad_entries.sort_by(|a, b| natural_sort_key(&a.0).cmp(&natural_sort_key(&b.0)));
    let package_pads: Vec<PackagePad> = pad_entries
        .iter()
        .map(|(name, uuid)| PackagePad {
            uuid: *uuid,
            name: name.clone(),
        })
        .collect();

    // Parse graphics
    let mut polygons = Vec::new();
    let mut texts = Vec::new();

    for child in node.children() {
        match child.keyword() {
            Some("fp_line") => {
                if let Some(poly) = parse_fp_line(child) {
                    polygons.push(poly);
                }
            }
            Some("fp_rect") => {
                if let Some(poly) = parse_fp_rect(child) {
                    polygons.push(poly);
                }
            }
            Some("fp_poly") => {
                if let Some(poly) = parse_fp_poly(child) {
                    polygons.push(poly);
                }
            }
            Some("fp_circle") => {
                if let Some(poly) = parse_fp_circle(child) {
                    polygons.push(poly);
                }
            }
            Some("fp_arc") => {
                if let Some(poly) = parse_fp_arc(child) {
                    polygons.push(poly);
                }
            }
            Some("fp_text") => {
                if let Some(text) = parse_fp_text(child) {
                    texts.push(text);
                }
            }
            Some("property") => {
                if let Some(text) = parse_property_text(child) {
                    texts.push(text);
                }
            }
            _ => {}
        }
    }

    // Determine assembly type
    let assembly_type = if has_tht && has_smd {
        AssemblyType::Mixed
    } else if has_tht {
        AssemblyType::Tht
    } else if has_smd {
        AssemblyType::Smt
    } else {
        AssemblyType::Auto
    };

    let footprint = Footprint {
        uuid: new_uuid(),
        name: "default".into(),
        description: String::new(),
        model_position: Position3D::default(),
        model_rotation: Position3D::default(),
        pads: footprint_pads,
        polygons,
        texts,
    };

    Ok(Package {
        meta: LibraryMeta {
            uuid: pkg_uuid,
            name: fp_name,
            description,
            keywords,
            author: "KiCad import".into(),
            version: "1.0".into(),
            created: now,
            deprecated: false,
            category: None,
        },
        assembly_type,
        grid_interval: 2.54,
        min_copper_clearance: 0.2,
        pads: package_pads,
        footprints: vec![footprint],
    })
}

// ---------------------------------------------------------------------------
// Metadata extraction
// ---------------------------------------------------------------------------

fn get_property(node: &SExpr, keyword: &str) -> Option<String> {
    // KiCad 8+ uses (property "key" "value") format
    for child in node.children() {
        if child.keyword() == Some("property") {
            let args = child.args();
            let key = args
                .first()
                .and_then(|a| a.as_str().or_else(|| a.as_atom()));
            if key == Some(keyword) {
                return args
                    .get(1)
                    .and_then(|a| a.as_str().or_else(|| a.as_atom()))
                    .map(|s| s.to_string());
            }
        }
    }
    // Legacy format: (descr "...") or (tags "...")
    node.child(keyword)
        .and_then(|c| {
            c.args()
                .first()
                .and_then(|a| a.as_str().or_else(|| a.as_atom()))
                .map(|s| s.to_string())
        })
}

// ---------------------------------------------------------------------------
// Layer mapping
// ---------------------------------------------------------------------------

fn map_kicad_layer(name: &str) -> Option<Layer> {
    match name {
        // Silkscreen
        "F.SilkS" | "F.Silkscreen" => Some(Layer::TopLegend),
        "B.SilkS" | "B.Silkscreen" => Some(Layer::BottomLegend),
        // Fabrication
        "F.Fab" => Some(Layer::TopDocumentation),
        "B.Fab" => Some(Layer::BottomDocumentation),
        // Courtyard
        "F.CrtYd" | "F.Courtyard" => Some(Layer::TopCourtyard),
        "B.CrtYd" | "B.Courtyard" => Some(Layer::BottomCourtyard),
        // Copper
        "F.Cu" => Some(Layer::TopCopper),
        "B.Cu" => Some(Layer::BottomCopper),
        // Edge cuts
        "Edge.Cuts" => Some(Layer::BoardOutlines),
        // Paste
        "F.Paste" => Some(Layer::TopSolderPaste),
        "B.Paste" => Some(Layer::BottomSolderPaste),
        // Mask
        "F.Mask" => Some(Layer::TopStopMask),
        "B.Mask" => Some(Layer::BottomStopMask),
        _ => None,
    }
}

/// Get the layer from a node's `(layer ...)` child.
fn get_layer(node: &SExpr) -> Option<Layer> {
    let layer_node = node.child("layer")?;
    let layer_name = layer_node
        .args()
        .first()
        .and_then(|a| a.as_str().or_else(|| a.as_atom()))?;
    map_kicad_layer(layer_name)
}

/// Get the stroke width from a node's `(stroke (width ...))` or `(width ...)` child.
fn get_stroke_width(node: &SExpr) -> f64 {
    node.child("stroke")
        .and_then(|s| s.child("width"))
        .and_then(|w| w.args().first()?.as_atom()?.parse::<f64>().ok())
        .or_else(|| {
            node.child("width")
                .and_then(|w| w.args().first()?.as_atom()?.parse::<f64>().ok())
        })
        .unwrap_or(0.12)
}

/// Check if a node has (fill solid) or (fill yes).
fn is_filled(node: &SExpr) -> bool {
    node.child("fill")
        .and_then(|f| {
            f.args()
                .first()
                .and_then(|a| a.as_atom())
        })
        .is_some_and(|v| v == "solid" || v == "yes")
}

// ---------------------------------------------------------------------------
// Pad parsing
// ---------------------------------------------------------------------------

/// Parse a `(pad ...)` node into a FootprintPad.
/// Returns (FootprintPad, pad_name, is_through_hole).
fn parse_pad(
    node: &SExpr,
    package_pad_map: &mut HashMap<String, uuid::Uuid>,
) -> Option<(FootprintPad, String, bool)> {
    let children = node.children();
    // (pad "1" smd rect (at x y [rot]) (size w h) (layers ...) ...)
    // (pad "1" thru_hole circle (at x y [rot]) (size w h) (drill d) (layers ...) ...)

    // Pad number/name
    let pad_name = children
        .get(1)
        .and_then(|c| c.as_str().or_else(|| c.as_atom()))?
        .to_string();

    // Pad type
    let pad_type = children.get(2).and_then(|c| c.as_atom())?;
    let is_tht = matches!(pad_type, "thru_hole" | "np_thru_hole");

    // Pad shape
    let shape_str = children.get(3).and_then(|c| c.as_atom())?;

    // Position
    let at_node = node.child("at")?;
    let at_args = at_node.args();
    let x = at_args.first()?.as_atom()?.parse::<f64>().ok()?;
    let y = at_args.get(1)?.as_atom()?.parse::<f64>().ok()?;
    let rotation = at_args
        .get(2)
        .and_then(|a| a.as_atom()?.parse::<f64>().ok())
        .unwrap_or(0.0);

    // Size
    let size_node = node.child("size")?;
    let size_args = size_node.args();
    let width = size_args.first()?.as_atom()?.parse::<f64>().ok()?;
    let height = size_args.get(1)?.as_atom()?.parse::<f64>().ok()?;

    // Determine PadSide from type and layers
    let side = if is_tht {
        PadSide::ThroughHole
    } else {
        // Check layers to determine top vs bottom
        let layers = get_pad_layers(node);
        if layers.iter().any(|l| l.contains("B.Cu")) && !layers.iter().any(|l| l.contains("F.Cu"))
        {
            PadSide::Bottom
        } else {
            PadSide::Top
        }
    };

    // Determine PadShape and radius
    let (shape, radius) = match shape_str {
        "circle" => (PadShape::Round, 0.0),
        "oval" => {
            // Oval → RoundRect with radius = min(w,h)/2 (fully rounded ends)
            let r = width.min(height) / 2.0;
            (PadShape::RoundRect, r)
        }
        "rect" => (PadShape::RoundRect, 0.0),
        "roundrect" => {
            let ratio = node
                .child("roundrect_rratio")
                .and_then(|r| r.args().first()?.as_atom()?.parse::<f64>().ok())
                .unwrap_or(0.25);
            let r = ratio * width.min(height);
            (PadShape::RoundRect, r)
        }
        "custom" => (PadShape::Custom, 0.0),
        "trapezoid" => (PadShape::RoundRect, 0.0),
        _ => (PadShape::RoundRect, 0.0),
    };

    // Drill hole
    let holes = if let Some(drill_node) = node.child("drill") {
        parse_drill(drill_node)
    } else {
        vec![]
    };

    // Get or create PackagePad UUID
    let package_pad_uuid = if pad_type == "np_thru_hole" {
        // Non-plated through-hole: no electrical connection, use a nil UUID
        uuid::Uuid::nil()
    } else {
        *package_pad_map
            .entry(pad_name.clone())
            .or_insert_with(new_uuid)
    };

    // Determine pad function
    let function = if pad_type == "np_thru_hole" {
        PadFunction::Unspecified
    } else {
        PadFunction::Standard
    };

    // Solder paste: off for THT pads, auto for SMD
    let solder_paste = if is_tht {
        SolderPasteConfig::Off
    } else {
        SolderPasteConfig::Auto
    };

    let fp_pad = FootprintPad {
        uuid: new_uuid(),
        package_pad: package_pad_uuid,
        side,
        shape,
        position: Position::new(x, y),
        rotation: Angle(rotation),
        width,
        height,
        radius,
        stop_mask: StopMaskConfig::Auto,
        solder_paste,
        clearance: 0.0,
        function,
        holes,
    };

    Some((fp_pad, pad_name, is_tht))
}

/// Get layer names from a pad's `(layers ...)` child.
fn get_pad_layers(node: &SExpr) -> Vec<String> {
    let Some(layers_node) = node.child("layers") else {
        return vec![];
    };
    layers_node
        .args()
        .iter()
        .filter_map(|a| a.as_str().or_else(|| a.as_atom()).map(|s| s.to_string()))
        .collect()
}

/// Parse a `(drill ...)` node into PadHole(s).
fn parse_drill(node: &SExpr) -> Vec<PadHole> {
    let args = node.args();
    if args.is_empty() {
        return vec![];
    }

    // Check for oval drill: (drill oval w h)
    let (diameter, is_oval) = if args.first().and_then(|a| a.as_atom()) == Some("oval") {
        // Oval drill: use the first dimension as diameter
        let w = args
            .get(1)
            .and_then(|a| a.as_atom()?.parse::<f64>().ok())
            .unwrap_or(0.0);
        (w, true)
    } else {
        let d = args
            .first()
            .and_then(|a| a.as_atom()?.parse::<f64>().ok())
            .unwrap_or(0.0);
        (d, false)
    };

    if diameter <= 0.0 {
        return vec![];
    }

    // For oval drills, build a path with two vertices (slot)
    let path = if is_oval {
        let h = args
            .get(2)
            .and_then(|a| a.as_atom()?.parse::<f64>().ok())
            .unwrap_or(diameter);
        if (h - diameter).abs() > 0.001 {
            // Slot: path from -offset to +offset along the longer axis
            let half_slot = (h - diameter) / 2.0;
            vec![
                Vertex {
                    position: Position::new(0.0, -half_slot),
                    angle: Angle(0.0),
                },
                Vertex {
                    position: Position::new(0.0, half_slot),
                    angle: Angle(0.0),
                },
            ]
        } else {
            vec![]
        }
    } else {
        vec![]
    };

    vec![PadHole {
        uuid: new_uuid(),
        diameter,
        path,
    }]
}

// ---------------------------------------------------------------------------
// Line / rectangle / polygon / circle / arc parsing
// ---------------------------------------------------------------------------

fn parse_fp_line(node: &SExpr) -> Option<Polygon> {
    let layer = get_layer(node)?;
    let width = get_stroke_width(node);

    let start = node.child("start")?;
    let end = node.child("end")?;
    let x1 = start.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let y1 = start.args().get(1)?.as_atom()?.parse::<f64>().ok()?;
    let x2 = end.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let y2 = end.args().get(1)?.as_atom()?.parse::<f64>().ok()?;

    Some(Polygon {
        uuid: new_uuid(),
        layer,
        width,
        fill: false,
        grab_area: false,
        vertices: vec![
            Vertex {
                position: Position::new(x1, y1),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(x2, y2),
                angle: Angle(0.0),
            },
        ],
    })
}

fn parse_fp_rect(node: &SExpr) -> Option<Polygon> {
    let layer = get_layer(node)?;
    let width = get_stroke_width(node);
    let fill = is_filled(node);

    let start = node.child("start")?;
    let end = node.child("end")?;
    let x1 = start.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let y1 = start.args().get(1)?.as_atom()?.parse::<f64>().ok()?;
    let x2 = end.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let y2 = end.args().get(1)?.as_atom()?.parse::<f64>().ok()?;

    Some(Polygon {
        uuid: new_uuid(),
        layer,
        width,
        fill,
        grab_area: false,
        vertices: vec![
            Vertex {
                position: Position::new(x1, y1),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(x2, y1),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(x2, y2),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(x1, y2),
                angle: Angle(0.0),
            },
            Vertex {
                position: Position::new(x1, y1),
                angle: Angle(0.0),
            },
        ],
    })
}

fn parse_fp_poly(node: &SExpr) -> Option<Polygon> {
    let layer = get_layer(node)?;
    let width = get_stroke_width(node);
    let fill = is_filled(node);

    let pts_node = node.child("pts")?;
    let mut vertices = Vec::new();
    for child in pts_node.children() {
        if child.keyword() == Some("xy") {
            let args = child.args();
            let x = args.first()?.as_atom()?.parse::<f64>().ok()?;
            let y = args.get(1)?.as_atom()?.parse::<f64>().ok()?;
            vertices.push(Vertex {
                position: Position::new(x, y),
                angle: Angle(0.0),
            });
        }
    }

    if vertices.is_empty() {
        return None;
    }

    Some(Polygon {
        uuid: new_uuid(),
        layer,
        width,
        fill,
        grab_area: false,
        vertices,
    })
}

fn parse_fp_circle(node: &SExpr) -> Option<Polygon> {
    let layer = get_layer(node)?;
    let width = get_stroke_width(node);
    let fill = is_filled(node);

    let center = node.child("center")?;
    let end = node.child("end")?;
    let cx = center.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let cy = center.args().get(1)?.as_atom()?.parse::<f64>().ok()?;
    let ex = end.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let ey = end.args().get(1)?.as_atom()?.parse::<f64>().ok()?;

    let radius = ((ex - cx).powi(2) + (ey - cy).powi(2)).sqrt();

    // Approximate circle as 4 quarter-arc vertices with 90° arc angles.
    // Each vertex has a 90° arc angle which, in the Volt convention, produces
    // a circular arc to the next vertex.
    Some(Polygon {
        uuid: new_uuid(),
        layer,
        width,
        fill,
        grab_area: false,
        vertices: vec![
            Vertex {
                position: Position::new(cx + radius, cy),
                angle: Angle(-90.0),
            },
            Vertex {
                position: Position::new(cx, cy + radius),
                angle: Angle(-90.0),
            },
            Vertex {
                position: Position::new(cx - radius, cy),
                angle: Angle(-90.0),
            },
            Vertex {
                position: Position::new(cx, cy - radius),
                angle: Angle(-90.0),
            },
            Vertex {
                position: Position::new(cx + radius, cy),
                angle: Angle(0.0),
            },
        ],
    })
}

fn parse_fp_arc(node: &SExpr) -> Option<Polygon> {
    let layer = get_layer(node)?;
    let width = get_stroke_width(node);

    let start = node.child("start")?;
    let mid = node.child("mid")?;
    let end = node.child("end")?;

    let sx = start.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let sy = start.args().get(1)?.as_atom()?.parse::<f64>().ok()?;
    let mx = mid.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let my = mid.args().get(1)?.as_atom()?.parse::<f64>().ok()?;
    let ex = end.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let ey = end.args().get(1)?.as_atom()?.parse::<f64>().ok()?;

    // Compute arc angle from 3 points: start, mid, end
    // Find the circumscribed circle center, then compute the swept angle.
    let angle = arc_angle_from_three_points(sx, sy, mx, my, ex, ey);

    Some(Polygon {
        uuid: new_uuid(),
        layer,
        width,
        fill: false,
        grab_area: false,
        vertices: vec![
            Vertex {
                position: Position::new(sx, sy),
                angle: Angle(angle),
            },
            Vertex {
                position: Position::new(ex, ey),
                angle: Angle(0.0),
            },
        ],
    })
}

/// Compute the arc sweep angle (in degrees) given three points on a circular arc.
fn arc_angle_from_three_points(
    sx: f64, sy: f64,
    mx: f64, my: f64,
    ex: f64, ey: f64,
) -> f64 {
    // Find circumscribed circle center using perpendicular bisectors
    let ax = sx;
    let ay = sy;
    let bx = mx;
    let by = my;
    let cx = ex;
    let cy = ey;

    let d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if d.abs() < 1e-10 {
        // Degenerate (collinear points) — just return 0
        return 0.0;
    }

    let ux = ((ax * ax + ay * ay) * (by - cy)
        + (bx * bx + by * by) * (cy - ay)
        + (cx * cx + cy * cy) * (ay - by))
        / d;
    let uy = ((ax * ax + ay * ay) * (cx - bx)
        + (bx * bx + by * by) * (ax - cx)
        + (cx * cx + cy * cy) * (bx - ax))
        / d;

    // Angles from center to start and end
    let angle_start = (sy - uy).atan2(sx - ux);
    let angle_end = (ey - uy).atan2(ex - ux);
    let angle_mid = (my - uy).atan2(mx - ux);

    // Determine sweep direction using mid point
    let mut sweep = angle_end - angle_start;
    // Normalize to [-2π, 2π]
    while sweep > std::f64::consts::PI {
        sweep -= 2.0 * std::f64::consts::PI;
    }
    while sweep < -std::f64::consts::PI {
        sweep += 2.0 * std::f64::consts::PI;
    }

    // Check if mid point is on the arc in the sweep direction
    let mut mid_angle = angle_mid - angle_start;
    while mid_angle > std::f64::consts::PI {
        mid_angle -= 2.0 * std::f64::consts::PI;
    }
    while mid_angle < -std::f64::consts::PI {
        mid_angle += 2.0 * std::f64::consts::PI;
    }

    // If mid is not between start and end in the sweep direction, flip
    if sweep > 0.0 && mid_angle < 0.0 {
        sweep -= 2.0 * std::f64::consts::PI;
    } else if sweep < 0.0 && mid_angle > 0.0 {
        sweep += 2.0 * std::f64::consts::PI;
    }

    sweep.to_degrees()
}

// ---------------------------------------------------------------------------
// Text parsing
// ---------------------------------------------------------------------------

fn parse_fp_text(node: &SExpr) -> Option<StrokeText> {
    let children = node.children();
    // (fp_text reference "REF**" (at x y [rot]) (layer ...) (effects ...))
    // (fp_text value "VAL**" (at x y [rot]) (layer ...) (effects ...))

    let text_type = children.get(1).and_then(|c| c.as_atom())?;

    let text_value = children
        .get(2)
        .and_then(|c| c.as_str().or_else(|| c.as_atom()))
        .unwrap_or("")
        .to_string();

    // Map to placeholder values
    let value = match text_type {
        "reference" => "{{NAME}}".to_string(),
        "value" => "{{VALUE}}".to_string(),
        _ => text_value,
    };

    let layer = get_layer(node).unwrap_or(Layer::TopLegend);

    let at_node = node.child("at")?;
    let at_args = at_node.args();
    let x = at_args.first()?.as_atom()?.parse::<f64>().ok()?;
    let y = at_args.get(1)?.as_atom()?.parse::<f64>().ok()?;
    let rotation = at_args
        .get(2)
        .and_then(|a| a.as_atom()?.parse::<f64>().ok())
        .unwrap_or(0.0);

    let effects = node.child("effects");
    let (height, stroke_width) = parse_effects_font(effects);

    let (h_align, v_align, mirror) = parse_effects_justify(effects);

    Some(StrokeText {
        uuid: new_uuid(),
        layer,
        value,
        position: Position::new(x, y),
        rotation: Angle(rotation),
        height,
        stroke_width,
        letter_spacing: None,
        line_spacing: None,
        align: Alignment {
            h: h_align,
            v: v_align,
        },
        mirror,
        auto_rotate: true,
        lock: false,
    })
}

/// Parse a KiCad 8+ `(property "Reference" "REF**" ...)` node as a StrokeText.
fn parse_property_text(node: &SExpr) -> Option<StrokeText> {
    let args = node.args();
    let prop_name = args.first().and_then(|a| a.as_str().or_else(|| a.as_atom()))?;

    // Only handle Reference and Value properties
    let value = match prop_name {
        "Reference" => "{{NAME}}".to_string(),
        "Value" => "{{VALUE}}".to_string(),
        _ => return None,
    };

    let layer = get_layer(node).unwrap_or(Layer::TopLegend);

    let at_node = node.child("at")?;
    let at_args = at_node.args();
    let x = at_args.first()?.as_atom()?.parse::<f64>().ok()?;
    let y = at_args.get(1)?.as_atom()?.parse::<f64>().ok()?;
    let rotation = at_args
        .get(2)
        .and_then(|a| a.as_atom()?.parse::<f64>().ok())
        .unwrap_or(0.0);

    let effects = node.child("effects");
    let (height, stroke_width) = parse_effects_font(effects);
    let (h_align, v_align, mirror) = parse_effects_justify(effects);

    Some(StrokeText {
        uuid: new_uuid(),
        layer,
        value,
        position: Position::new(x, y),
        rotation: Angle(rotation),
        height,
        stroke_width,
        letter_spacing: None,
        line_spacing: None,
        align: Alignment {
            h: h_align,
            v: v_align,
        },
        mirror,
        auto_rotate: true,
        lock: false,
    })
}

fn parse_effects_font(effects: Option<&SExpr>) -> (f64, f64) {
    let font = effects.and_then(|e| e.child("font"));
    let height = font
        .and_then(|f| f.child("size"))
        .and_then(|s| s.args().get(1).or_else(|| s.args().first()))
        .and_then(|a| a.as_atom()?.parse::<f64>().ok())
        .unwrap_or(1.0);
    let stroke_width = font
        .and_then(|f| f.child("thickness"))
        .and_then(|t| t.args().first()?.as_atom()?.parse::<f64>().ok())
        .unwrap_or(0.15);
    (height, stroke_width)
}

fn parse_effects_justify(effects: Option<&SExpr>) -> (HAlign, VAlign, bool) {
    let justify_args = effects
        .and_then(|e| e.child("justify"))
        .map(|j| j.args())
        .unwrap_or(&[]);

    let h = if justify_args
        .iter()
        .any(|a| a.as_atom() == Some("right"))
    {
        HAlign::Right
    } else if justify_args
        .iter()
        .any(|a| a.as_atom() == Some("left"))
    {
        HAlign::Left
    } else {
        HAlign::Center
    };
    let v = if justify_args
        .iter()
        .any(|a| a.as_atom() == Some("top"))
    {
        VAlign::Top
    } else if justify_args
        .iter()
        .any(|a| a.as_atom() == Some("bottom"))
    {
        VAlign::Bottom
    } else {
        VAlign::Center
    };
    let mirror = justify_args
        .iter()
        .any(|a| a.as_atom() == Some("mirror"));

    (h, v, mirror)
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Natural sort key: split a string into numeric and non-numeric parts
/// for human-friendly ordering (e.g., "1", "2", "10" instead of "1", "10", "2").
fn natural_sort_key(s: &str) -> Vec<NaturalSortPart> {
    let mut parts = Vec::new();
    let mut chars = s.chars().peekable();
    while chars.peek().is_some() {
        if chars.peek().is_some_and(|c| c.is_ascii_digit()) {
            let mut num = String::new();
            while chars.peek().is_some_and(|c| c.is_ascii_digit()) {
                num.push(chars.next().unwrap());
            }
            parts.push(NaturalSortPart::Num(num.parse().unwrap_or(0)));
        } else {
            let mut text = String::new();
            while chars.peek().is_some_and(|c| !c.is_ascii_digit()) {
                text.push(chars.next().unwrap());
            }
            parts.push(NaturalSortPart::Text(text));
        }
    }
    parts
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
enum NaturalSortPart {
    Num(u64),
    Text(String),
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_smd_0805_resistor() {
        let input = r#"(footprint "R_0805_2012Metric"
  (version 20240108)
  (generator "pcbnew")
  (layer "F.Cu")
  (descr "Resistor SMD 0805 (2012 Metric)")
  (tags "resistor 0805")
  (attr smd)
  (fp_text reference "REF**" (at 0 -1.43) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15)))
  )
  (fp_text value "R_0805" (at 0 1.43) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15)))
  )
  (fp_line (start -1 0.62) (end -1 -0.62) (layer "F.SilkS") (stroke (width 0.12) (type solid)))
  (fp_line (start -1 -0.62) (end 1 -0.62) (layer "F.SilkS") (stroke (width 0.12) (type solid)))
  (fp_line (start 1 -0.62) (end 1 0.62) (layer "F.SilkS") (stroke (width 0.12) (type solid)))
  (fp_line (start 1 0.62) (end -1 0.62) (layer "F.SilkS") (stroke (width 0.12) (type solid)))
  (fp_rect (start -1.68 -0.95) (end 1.68 0.95) (layer "F.CrtYd") (stroke (width 0.05) (type solid)))
  (fp_line (start -0.26 0.28) (end -0.26 -0.28) (layer "F.Fab") (stroke (width 0.1) (type solid)))
  (pad "1" smd roundrect (at -0.9125 0) (size 1.025 1.4) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.243902))
  (pad "2" smd roundrect (at 0.9125 0) (size 1.025 1.4) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.243902))
)"#;

        let pkg = parse_kicad_mod(input).unwrap();

        // Metadata
        assert_eq!(pkg.meta.name, "R_0805_2012Metric");
        assert_eq!(pkg.meta.description, "Resistor SMD 0805 (2012 Metric)");
        assert_eq!(pkg.meta.keywords, "resistor 0805");
        assert_eq!(pkg.assembly_type, AssemblyType::Smt);

        // Package pads
        assert_eq!(pkg.pads.len(), 2);
        assert_eq!(pkg.pads[0].name, "1");
        assert_eq!(pkg.pads[1].name, "2");

        // Footprint
        assert_eq!(pkg.footprints.len(), 1);
        let fp = &pkg.footprints[0];
        assert_eq!(fp.name, "default");

        // Footprint pads
        assert_eq!(fp.pads.len(), 2);

        let pad1 = &fp.pads[0];
        assert_eq!(pad1.side, PadSide::Top);
        assert_eq!(pad1.shape, PadShape::RoundRect);
        assert!((pad1.position.x - (-0.9125)).abs() < 0.001);
        assert!((pad1.position.y - 0.0).abs() < 0.001);
        assert!((pad1.width - 1.025).abs() < 0.001);
        assert!((pad1.height - 1.4).abs() < 0.001);
        assert!(pad1.radius > 0.0); // roundrect has nonzero radius
        assert!(pad1.holes.is_empty());

        let pad2 = &fp.pads[1];
        assert_eq!(pad2.side, PadSide::Top);
        assert!((pad2.position.x - 0.9125).abs() < 0.001);

        // Polygons — silkscreen lines + courtyard rect + fab line
        // 4 silkscreen lines + 1 courtyard rect + 1 fab line = 6
        assert_eq!(fp.polygons.len(), 6);

        // Check silkscreen lines exist
        let silk_polys: Vec<_> = fp
            .polygons
            .iter()
            .filter(|p| p.layer == Layer::TopLegend)
            .collect();
        assert_eq!(silk_polys.len(), 4);

        // Check courtyard rect exists
        let crtyd_polys: Vec<_> = fp
            .polygons
            .iter()
            .filter(|p| p.layer == Layer::TopCourtyard)
            .collect();
        assert_eq!(crtyd_polys.len(), 1);
        assert_eq!(crtyd_polys[0].vertices.len(), 5); // closed rect

        // Texts
        assert_eq!(fp.texts.len(), 2);
        assert!(fp.texts.iter().any(|t| t.value == "{{NAME}}"));
        assert!(fp.texts.iter().any(|t| t.value == "{{VALUE}}"));
    }

    #[test]
    fn parses_tht_2pin_footprint() {
        let input = r#"(footprint "PinHeader_1x02_P2.54mm_Vertical"
  (version 20240108)
  (generator "pcbnew")
  (layer "F.Cu")
  (descr "Through hole pin header, 1x02, 2.54mm pitch, vertical")
  (tags "pin header connector through hole")
  (attr through_hole)
  (fp_text reference "REF**" (at 0 -2.33) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15)))
  )
  (fp_text value "PinHeader_1x02" (at 0 4.87) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15)))
  )
  (fp_line (start -1.33 -1.33) (end 0 -1.33) (layer "F.SilkS") (stroke (width 0.12) (type solid)))
  (fp_line (start -1.33 -1.33) (end -1.33 3.87) (layer "F.SilkS") (stroke (width 0.12) (type solid)))
  (fp_line (start -1.33 3.87) (end 1.33 3.87) (layer "F.SilkS") (stroke (width 0.12) (type solid)))
  (fp_line (start 1.33 3.87) (end 1.33 -1.33) (layer "F.SilkS") (stroke (width 0.12) (type solid)))
  (fp_rect (start -1.8 -1.8) (end 1.8 4.35) (layer "F.CrtYd") (stroke (width 0.05) (type solid)))
  (fp_rect (start -1.27 -1.27) (end 1.27 3.81) (layer "F.Fab") (stroke (width 0.1) (type solid)))
  (pad "1" thru_hole rect (at 0 0) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))
  (pad "2" thru_hole oval (at 0 2.54) (size 1.7 1.7) (drill 1.0) (layers "*.Cu" "*.Mask"))
)"#;

        let pkg = parse_kicad_mod(input).unwrap();

        // Metadata
        assert_eq!(pkg.meta.name, "PinHeader_1x02_P2.54mm_Vertical");
        assert_eq!(pkg.assembly_type, AssemblyType::Tht);

        // Package pads
        assert_eq!(pkg.pads.len(), 2);
        assert_eq!(pkg.pads[0].name, "1");
        assert_eq!(pkg.pads[1].name, "2");

        // Footprint pads
        let fp = &pkg.footprints[0];
        assert_eq!(fp.pads.len(), 2);

        // Pad 1: thru_hole rect
        let pad1 = &fp.pads[0];
        assert_eq!(pad1.side, PadSide::ThroughHole);
        assert_eq!(pad1.shape, PadShape::RoundRect);
        assert!((pad1.radius - 0.0).abs() < 0.001); // rect → radius 0
        assert!((pad1.position.x - 0.0).abs() < 0.001);
        assert!((pad1.position.y - 0.0).abs() < 0.001);
        assert_eq!(pad1.holes.len(), 1);
        assert!((pad1.holes[0].diameter - 1.0).abs() < 0.001);

        // Pad 2: thru_hole oval
        let pad2 = &fp.pads[1];
        assert_eq!(pad2.side, PadSide::ThroughHole);
        assert_eq!(pad2.shape, PadShape::RoundRect);
        assert!(pad2.radius > 0.0); // oval → has radius
        assert!((pad2.position.y - 2.54).abs() < 0.001);
        assert_eq!(pad2.holes.len(), 1);
        assert!((pad2.holes[0].diameter - 1.0).abs() < 0.001);

        // Solder paste off for THT
        assert_eq!(pad1.solder_paste, SolderPasteConfig::Off);
        assert_eq!(pad2.solder_paste, SolderPasteConfig::Off);

        // Polygons: 4 silkscreen lines + 1 courtyard rect + 1 fab rect = 6
        assert_eq!(fp.polygons.len(), 6);
    }

    #[test]
    fn parses_module_legacy_keyword() {
        let input = r#"(module "R_0603"
  (layer "F.Cu")
  (descr "0603 resistor")
  (pad "1" smd rect (at -0.75 0) (size 0.8 0.8) (layers "F.Cu" "F.Paste" "F.Mask"))
  (pad "2" smd rect (at 0.75 0) (size 0.8 0.8) (layers "F.Cu" "F.Paste" "F.Mask"))
)"#;

        let pkg = parse_kicad_mod(input).unwrap();
        assert_eq!(pkg.meta.name, "R_0603");
        assert_eq!(pkg.pads.len(), 2);
        assert_eq!(pkg.footprints[0].pads.len(), 2);
    }

    #[test]
    fn parses_npth_pad() {
        let input = r#"(footprint "MountingHole_3.2mm"
  (layer "F.Cu")
  (descr "Mounting hole 3.2mm")
  (pad "" np_thru_hole circle (at 0 0) (size 3.2 3.2) (drill 3.2) (layers "*.Cu" "*.Mask"))
)"#;

        let pkg = parse_kicad_mod(input).unwrap();
        // No-pad mounting hole has no package pads
        assert_eq!(pkg.pads.len(), 0);
        assert_eq!(pkg.footprints[0].pads.len(), 1);
        let pad = &pkg.footprints[0].pads[0];
        assert_eq!(pad.side, PadSide::ThroughHole);
        assert_eq!(pad.shape, PadShape::Round);
        assert_eq!(pad.package_pad, uuid::Uuid::nil());
        assert_eq!(pad.holes.len(), 1);
        assert!((pad.holes[0].diameter - 3.2).abs() < 0.001);
    }

    #[test]
    fn parses_polygon_and_circle() {
        let input = r#"(footprint "TestPoly"
  (layer "F.Cu")
  (fp_poly (pts (xy 0 0) (xy 1 0) (xy 1 1) (xy 0 1) (xy 0 0)) (layer "F.SilkS") (stroke (width 0.12) (type solid)) (fill solid))
  (fp_circle (center 0 0) (end 1 0) (layer "F.Fab") (stroke (width 0.1) (type solid)))
)"#;

        let pkg = parse_kicad_mod(input).unwrap();
        let fp = &pkg.footprints[0];

        assert_eq!(fp.polygons.len(), 2);

        // Polygon
        let poly = &fp.polygons[0];
        assert_eq!(poly.layer, Layer::TopLegend);
        assert_eq!(poly.vertices.len(), 5);
        assert!(poly.fill);

        // Circle approximation (4 arcs + closing vertex)
        let circle = &fp.polygons[1];
        assert_eq!(circle.layer, Layer::TopDocumentation);
        assert_eq!(circle.vertices.len(), 5);
    }

    #[test]
    fn parses_kicad8_property_syntax() {
        let input = r#"(footprint "C_0402_1005Metric"
  (version 20240108)
  (layer "F.Cu")
  (property "Reference" "REF**" (at 0 -1.43) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15)))
  )
  (property "Value" "C_0402" (at 0 1.43) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15)))
  )
  (property "Description" "Capacitor SMD 0402")
  (pad "1" smd roundrect (at -0.48 0) (size 0.56 0.62) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
  (pad "2" smd roundrect (at 0.48 0) (size 0.56 0.62) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
)"#;

        let pkg = parse_kicad_mod(input).unwrap();
        assert_eq!(pkg.meta.name, "C_0402_1005Metric");
        assert_eq!(pkg.meta.description, "Capacitor SMD 0402");

        // Should have texts from property nodes
        let fp = &pkg.footprints[0];
        assert!(fp.texts.iter().any(|t| t.value == "{{NAME}}"));
        assert!(fp.texts.iter().any(|t| t.value == "{{VALUE}}"));
    }

    #[test]
    fn pad_names_naturally_sorted() {
        let input = r#"(footprint "IC_Test"
  (layer "F.Cu")
  (pad "10" smd rect (at 0 0) (size 0.5 0.5) (layers "F.Cu" "F.Paste" "F.Mask"))
  (pad "2" smd rect (at 1 0) (size 0.5 0.5) (layers "F.Cu" "F.Paste" "F.Mask"))
  (pad "1" smd rect (at 2 0) (size 0.5 0.5) (layers "F.Cu" "F.Paste" "F.Mask"))
)"#;

        let pkg = parse_kicad_mod(input).unwrap();
        let names: Vec<&str> = pkg.pads.iter().map(|p| p.name.as_str()).collect();
        assert_eq!(names, vec!["1", "2", "10"]);
    }

    #[test]
    fn bottom_smd_pad_detection() {
        let input = r#"(footprint "BottomSmd"
  (layer "B.Cu")
  (pad "1" smd rect (at 0 0) (size 1 1) (layers "B.Cu" "B.Paste" "B.Mask"))
)"#;

        let pkg = parse_kicad_mod(input).unwrap();
        let pad = &pkg.footprints[0].pads[0];
        assert_eq!(pad.side, PadSide::Bottom);
    }
}
