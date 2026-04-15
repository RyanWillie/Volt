//! KiCad `.kicad_sym` symbol library parser.
//!
//! Parses KiCad 8+ symbol files and converts them to `volt-core` types.
//!
//! KiCad symbol format structure:
//! - `(kicad_symbol_lib (symbol "NAME" ...))` — one symbol per file
//! - Properties: Reference, Value, Description, ki_keywords, ki_fp_filters
//! - Sub-symbols: `NAME_0_1` = graphics, `NAME_N_1` = pins for unit N
//! - Pin types: passive, input, output, power_in, bidirectional, etc.
//! - `(extends "OtherSymbol")` for derived symbols (metadata-only overrides)

use volt_core::common::*;
use volt_core::library::*;

use crate::sexp::{parse, SExpr};

/// Result of parsing a KiCad symbol file.
#[derive(Debug, Clone)]
pub struct KicadSymbolImport {
    /// The symbol (schematic visual).
    pub symbol: Symbol,
    /// The component (abstract electrical part).
    pub component: Component,
    /// Name of the parent symbol if this uses `(extends "...")`.
    pub extends: Option<String>,
}

/// Parse a `.kicad_sym` file and return the imported symbol + component.
pub fn parse_kicad_sym(content: &str) -> Result<KicadSymbolImport, String> {
    let root = parse(content).map_err(|e| format!("S-expr parse error: {e}"))?;

    // Root should be (kicad_symbol_lib ...)
    if root.keyword() != Some("kicad_symbol_lib") {
        return Err("Expected kicad_symbol_lib root element".into());
    }

    // Find the (symbol ...) child
    let sym_node = root.child("symbol")
        .ok_or("No symbol element found")?;

    parse_symbol_node(sym_node)
}

fn parse_symbol_node(node: &SExpr) -> Result<KicadSymbolImport, String> {
    let children = node.children();
    if children.len() < 2 {
        return Err("Symbol node too short".into());
    }

    // First arg is the symbol name
    let sym_name = children.get(1)
        .and_then(|c| c.as_str().or_else(|| c.as_atom()))
        .ok_or("Missing symbol name")?
        .to_string();

    // Check for (extends "ParentName")
    let extends = node.child("extends")
        .and_then(|e| e.args().first())
        .and_then(|a| a.as_str().or_else(|| a.as_atom()))
        .map(|s| s.to_string());

    // Extract properties
    let reference = get_property(node, "Reference").unwrap_or_default();
    let value = get_property(node, "Value").unwrap_or_default();
    let description = get_property(node, "Description").unwrap_or_default();
    let keywords = get_property(node, "ki_keywords").unwrap_or_default();
    let _fp_filters = get_property(node, "ki_fp_filters").unwrap_or_default();
    let _datasheet = get_property(node, "Datasheet").unwrap_or_default();

    let now = chrono::Utc::now();
    let sym_uuid = new_uuid();
    let comp_uuid = new_uuid();

    // Parse sub-symbols for graphics and pins
    let mut polygons = Vec::new();
    let mut all_pins: Vec<(u32, SymbolPin, SignalRole)> = Vec::new(); // (unit, pin, role)

    for child in node.children() {
        if child.keyword() != Some("symbol") {
            continue;
        }
        let sub_name = child.children().get(1)
            .and_then(|c| c.as_str().or_else(|| c.as_atom()))
            .unwrap_or("");

        // Parse unit and conversion from name like "R_0_1" or "LM2904_1_1"
        let (unit, _conversion) = parse_sub_symbol_name(sub_name, &sym_name);

        // Parse graphics (rectangles, polylines)
        for gc in child.children() {
            match gc.keyword() {
                Some("rectangle") => {
                    if let Some(poly) = parse_rectangle(gc) {
                        polygons.push(poly);
                    }
                }
                Some("polyline") => {
                    if let Some(poly) = parse_polyline(gc) {
                        polygons.push(poly);
                    }
                }
                Some("pin") => {
                    if let Some((pin, role)) = parse_pin(gc) {
                        all_pins.push((unit, pin, role));
                    }
                }
                _ => {}
            }
        }
    }

    // Build symbol (visual representation)
    let sym_pins: Vec<SymbolPin> = all_pins.iter().map(|(_, pin, _)| pin.clone()).collect();

    let symbol = Symbol {
        meta: LibraryMeta {
            uuid: sym_uuid,
            name: sym_name.clone(),
            description: description.clone(),
            keywords: keywords.clone(),
            author: "KiCad import".into(),
            version: "1.0".into(),
            created: now,
            deprecated: false,
            category: None,
        },
        pins: sym_pins,
        polygons,
        texts: vec![
            parse_property_text(node, "Reference", Layer::SchNames, "{{NAME}}")
                .unwrap_or(SymbolText {
                    uuid: new_uuid(),
                    layer: Layer::SchNames,
                    value: "{{NAME}}".into(),
                    position: Position::new(0.0, -1.27),
                    rotation: Angle(0.0),
                    height: 1.27,
                    align: Alignment { h: HAlign::Center, v: VAlign::Bottom },
                    lock: false,
                }),
            parse_property_text(node, "Value", Layer::SchValues, "{{VALUE}}")
                .unwrap_or(SymbolText {
                    uuid: new_uuid(),
                    layer: Layer::SchValues,
                    value: "{{VALUE}}".into(),
                    position: Position::new(0.0, 1.27),
                    rotation: Angle(0.0),
                    height: 1.27,
                    align: Alignment { h: HAlign::Center, v: VAlign::Top },
                    lock: false,
                }),
        ],
        grid_interval: 2.54,
    };

    // Build component (abstract electrical part)
    let signals: Vec<Signal> = all_pins.iter().map(|(_, pin, role)| {
        Signal {
            uuid: new_uuid(),
            name: pin.name.clone(),
            role: *role,
            required: matches!(role, SignalRole::Power | SignalRole::Input | SignalRole::Output),
            negated: false,
            clock: false,
            forced_net: String::new(),
        }
    }).collect();

    let pin_mappings: Vec<PinMapping> = symbol.pins.iter().zip(signals.iter())
        .map(|(pin, sig)| PinMapping {
            pin: pin.uuid,
            signal: sig.uuid,
        })
        .collect();

    let gate_uuid = new_uuid();
    let variant_uuid = new_uuid();

    let component = Component {
        meta: LibraryMeta {
            uuid: comp_uuid,
            name: sym_name.clone(),
            description,
            keywords,
            author: "KiCad import".into(),
            version: "1.0".into(),
            created: now,
            deprecated: false,
            category: None,
        },
        prefix: if reference.is_empty() { "U".into() } else { reference },
        default_value: value,
        schematic_only: false,
        attributes: vec![],
        signals,
        variants: vec![ComponentVariant {
            uuid: variant_uuid,
            norm: String::new(),
            name: "default".into(),
            description: String::new(),
            gates: vec![Gate {
                uuid: gate_uuid,
                symbol: sym_uuid,
                position: Position::default(),
                rotation: Angle::default(),
                required: true,
                suffix: String::new(),
                pin_mappings,
            }],
        }],
    };

    Ok(KicadSymbolImport {
        symbol,
        component,
        extends,
    })
}

// ---------------------------------------------------------------------------
// Property extraction
// ---------------------------------------------------------------------------

fn get_property(node: &SExpr, name: &str) -> Option<String> {
    get_property_node(node, name)?
        .args()
        .get(1)
        .and_then(|a| a.as_str().or_else(|| a.as_atom()))
        .map(|s| s.to_string())
}

fn get_property_node<'a>(node: &'a SExpr, name: &str) -> Option<&'a SExpr> {
    node.children().iter().find(|child| {
        if child.keyword() != Some("property") {
            return false;
        }
        child.args()
            .first()
            .and_then(|a| a.as_str().or_else(|| a.as_atom()))
            == Some(name)
    })
}

fn parse_property_text(node: &SExpr, name: &str, layer: Layer, placeholder: &str) -> Option<SymbolText> {
    let prop = get_property_node(node, name)?;
    let at = prop.child("at")?;
    let at_args = at.args();
    let x = at_args.first()?.as_atom()?.parse::<f64>().ok()?;
    let y = at_args.get(1)?.as_atom()?.parse::<f64>().ok()?;
    let rotation = at_args.get(2)
        .and_then(|a| a.as_atom()?.parse::<f64>().ok())
        .unwrap_or(0.0);

    let effects = prop.child("effects");
    let height = effects
        .and_then(|e| e.child("font"))
        .and_then(|f| f.child("size"))
        .and_then(|s| s.args().get(1).or_else(|| s.args().first()))
        .and_then(|a| a.as_atom()?.parse::<f64>().ok())
        .unwrap_or(1.27);

    let justify_args = effects
        .and_then(|e| e.child("justify"))
        .map(|j| j.args())
        .unwrap_or(&[]);

    let h = if justify_args.iter().any(|a| a.as_atom() == Some("right")) {
        HAlign::Right
    } else if justify_args.iter().any(|a| a.as_atom() == Some("center")) {
        HAlign::Center
    } else {
        HAlign::Left
    };
    let v = if justify_args.iter().any(|a| a.as_atom() == Some("top")) {
        VAlign::Top
    } else if justify_args.iter().any(|a| a.as_atom() == Some("bottom")) {
        VAlign::Bottom
    } else {
        VAlign::Center
    };

    Some(SymbolText {
        uuid: new_uuid(),
        layer,
        value: placeholder.into(),
        position: Position::new(x, y),
        rotation: Angle(rotation),
        height,
        align: Alignment { h, v },
        lock: false,
    })
}

// ---------------------------------------------------------------------------
// Sub-symbol name parsing
// ---------------------------------------------------------------------------

/// Parse "R_0_1" → (unit=0, conversion=1), "LM2904_2_1" → (unit=2, conversion=1)
fn parse_sub_symbol_name(sub_name: &str, parent_name: &str) -> (u32, u32) {
    let suffix = sub_name.strip_prefix(parent_name)
        .and_then(|s| s.strip_prefix('_'))
        .unwrap_or("");

    let parts: Vec<&str> = suffix.split('_').collect();
    let unit = parts.first().and_then(|s| s.parse().ok()).unwrap_or(0);
    let conversion = parts.get(1).and_then(|s| s.parse().ok()).unwrap_or(1);
    (unit, conversion)
}

// ---------------------------------------------------------------------------
// Graphics parsing
// ---------------------------------------------------------------------------

fn parse_rectangle(node: &SExpr) -> Option<Polygon> {
    let start = node.child("start")?;
    let end = node.child("end")?;

    let x1 = start.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let y1 = start.args().get(1)?.as_atom()?.parse::<f64>().ok()?;
    let x2 = end.args().first()?.as_atom()?.parse::<f64>().ok()?;
    let y2 = end.args().get(1)?.as_atom()?.parse::<f64>().ok()?;

    let width = node.child("stroke")
        .and_then(|s| s.child("width"))
        .and_then(|w| w.args().first()?.as_atom()?.parse::<f64>().ok())
        .unwrap_or(0.254);

    Some(Polygon {
        uuid: new_uuid(),
        layer: Layer::SchOutlines,
        width,
        fill: false,
        grab_area: true,
        vertices: vec![
            Vertex { position: Position::new(x1, y1), angle: Angle(0.0) },
            Vertex { position: Position::new(x2, y1), angle: Angle(0.0) },
            Vertex { position: Position::new(x2, y2), angle: Angle(0.0) },
            Vertex { position: Position::new(x1, y2), angle: Angle(0.0) },
            Vertex { position: Position::new(x1, y1), angle: Angle(0.0) },
        ],
    })
}

fn parse_polyline(node: &SExpr) -> Option<Polygon> {
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

    let width = node.child("stroke")
        .and_then(|s| s.child("width"))
        .and_then(|w| w.args().first()?.as_atom()?.parse::<f64>().ok())
        .unwrap_or(0.0);

    Some(Polygon {
        uuid: new_uuid(),
        layer: Layer::SchOutlines,
        width,
        fill: false,
        grab_area: false,
        vertices,
    })
}

// ---------------------------------------------------------------------------
// Pin parsing
// ---------------------------------------------------------------------------

fn parse_pin(node: &SExpr) -> Option<(SymbolPin, SignalRole)> {
    let children = node.children();
    // (pin <type> <shape> (at x y angle) (length l) (name "..." ...) (number "..." ...))

    // Pin type is the second element
    let pin_type = children.get(1)?.as_atom()?;
    let role = match pin_type {
        "passive" => SignalRole::Passive,
        "input" => SignalRole::Input,
        "output" => SignalRole::Output,
        "power_in" => SignalRole::Power,
        "power_out" => SignalRole::Power,
        "bidirectional" => SignalRole::Bidirectional,
        "open_collector" => SignalRole::OpenCollector,
        "open_emitter" => SignalRole::OpenDrain,
        _ => SignalRole::Passive,
    };

    // Position and angle
    let at_node = node.child("at")?;
    let at_args = at_node.args();
    let x = at_args.first()?.as_atom()?.parse::<f64>().ok()?;
    let y = at_args.get(1)?.as_atom()?.parse::<f64>().ok()?;
    let angle = at_args.get(2)
        .and_then(|a| a.as_atom()?.parse::<f64>().ok())
        .unwrap_or(0.0);

    // Length
    let length = node.child("length")
        .and_then(|l| l.args().first()?.as_atom()?.parse::<f64>().ok())
        .unwrap_or(2.54);

    // Pin number (the electrical identifier)
    let number = node.child("number")
        .and_then(|n| n.args().first())
        .and_then(|a| a.as_str().or_else(|| a.as_atom()))
        .unwrap_or("?")
        .to_string();

    // Pin name/function (display name)
    let pin_name = node.child("name")
        .and_then(|n| n.args().first())
        .and_then(|a| a.as_str().or_else(|| a.as_atom()))
        .unwrap_or("")
        .to_string();

    let pin = SymbolPin {
        uuid: new_uuid(),
        name: number,
        pin_name: if pin_name == "~" { String::new() } else { pin_name },
        position: Position::new(x, y),
        rotation: Angle(angle),
        length,
        name_position: Position::new(0.0, 0.0),
        name_rotation: Angle(0.0),
        name_height: 1.27,
        name_align: Alignment { h: HAlign::Left, v: VAlign::Center },
    };

    Some((pin, role))
}

// ---------------------------------------------------------------------------
// Bulk import helpers
// ---------------------------------------------------------------------------

/// Import all `.kicad_sym` files from a directory.
pub fn import_kicad_sym_dir(dir: &std::path::Path) -> Vec<Result<KicadSymbolImport, String>> {
    let mut results = Vec::new();

    fn walk(dir: &std::path::Path, results: &mut Vec<Result<KicadSymbolImport, String>>) {
        let Ok(entries) = std::fs::read_dir(dir) else { return };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() {
                walk(&path, results);
            } else if path.extension().is_some_and(|e| e == "kicad_sym") {
                let content = match std::fs::read_to_string(&path) {
                    Ok(c) => c,
                    Err(e) => {
                        results.push(Err(format!("{}: {e}", path.display())));
                        continue;
                    }
                };
                results.push(
                    parse_kicad_sym(&content)
                        .map_err(|e| format!("{}: {e}", path.display()))
                );
            }
        }
    }

    walk(dir, &mut results);
    results
}

/// Resolve KiCad `(extends "...")` inheritance by copying pins/graphics from
/// the parent symbol when the child defines metadata-only overrides.
pub fn resolve_extends(imports: Vec<KicadSymbolImport>) -> Result<Vec<KicadSymbolImport>, String> {
    use std::collections::HashMap;

    let by_name: HashMap<String, KicadSymbolImport> = imports
        .iter()
        .cloned()
        .map(|imp| (imp.symbol.meta.name.clone(), imp))
        .collect();

    imports
        .iter()
        .map(|imp| resolve_one(imp, &by_name, &mut Vec::new()))
        .collect()
}

fn resolve_one(
    imp: &KicadSymbolImport,
    by_name: &std::collections::HashMap<String, KicadSymbolImport>,
    stack: &mut Vec<String>,
) -> Result<KicadSymbolImport, String> {
    let Some(parent_name) = &imp.extends else {
        return Ok(imp.clone());
    };
    if stack.contains(&imp.symbol.meta.name) {
        return Err(format!("Cyclic symbol inheritance: {}", stack.join(" -> ")));
    }
    let parent = by_name.get(parent_name)
        .ok_or_else(|| format!("Missing parent symbol '{parent_name}' for '{}'", imp.symbol.meta.name))?;
    stack.push(imp.symbol.meta.name.clone());
    let resolved_parent = resolve_one(parent, by_name, stack)?;
    stack.pop();

    let mut resolved = imp.clone();
    if resolved.symbol.pins.is_empty() {
        resolved.symbol.pins = resolved_parent.symbol.pins;
    }
    if resolved.symbol.polygons.is_empty() {
        resolved.symbol.polygons = resolved_parent.symbol.polygons;
    }
    if resolved.component.signals.is_empty() {
        resolved.component.signals = resolved_parent.component.signals;
    }
    if resolved.component.variants.first().is_some_and(|v| v.gates.first().is_some_and(|g| g.pin_mappings.is_empty())) {
        if let (Some(variant), Some(parent_variant)) = (
            resolved.component.variants.first_mut(),
            resolved_parent.component.variants.first(),
        ) {
            if let (Some(gate), Some(parent_gate)) = (variant.gates.first_mut(), parent_variant.gates.first()) {
                gate.pin_mappings = parent_gate.pin_mappings.clone();
            }
        }
    }
    Ok(resolved)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_simple_resistor() {
        let input = r#"(kicad_symbol_lib
  (version 20251024)
  (symbol "R"
    (property "Reference" "R")
    (property "Value" "R")
    (property "Description" "Resistor")
    (property "ki_keywords" "res resistor")
    (symbol "R_0_1"
      (rectangle (start -1 -2) (end 1 2) (stroke (width 0.254)) (fill (type none)))
    )
    (symbol "R_1_1"
      (pin passive line (at 0 3.81 270) (length 1.27) (name "") (number "1"))
      (pin passive line (at 0 -3.81 90) (length 1.27) (name "") (number "2"))
    )
  )
)"#;

        let parsed = parse_kicad_sym(input).unwrap();
        assert_eq!(parsed.symbol.meta.name, "R");
        assert_eq!(parsed.symbol.pins.len(), 2);
        assert_eq!(parsed.symbol.pins[0].name, "1");
        assert_eq!(parsed.symbol.pins[0].pin_name, "");
        assert_eq!(parsed.component.signals.len(), 2);
        assert_eq!(parsed.component.prefix, "R");
    }

    #[test]
    fn resolves_extends_from_parent() {
        let parent = parse_kicad_sym(r#"(kicad_symbol_lib
  (version 1)
  (symbol "BASE"
    (property "Reference" "U")
    (property "Value" "BASE")
    (symbol "BASE_1_1"
      (pin input line (at -2.54 0 0) (length 2.54) (name "IN") (number "1"))
      (pin output line (at 2.54 0 180) (length 2.54) (name "OUT") (number "2"))
    )
  )
)"#).unwrap();
        let child = parse_kicad_sym(r#"(kicad_symbol_lib
  (version 1)
  (symbol "DERIVED"
    (extends "BASE")
    (property "Reference" "U")
    (property "Value" "DERIVED")
    (property "Description" "Child")
  )
)"#).unwrap();

        let resolved = resolve_extends(vec![parent, child]).unwrap();
        let derived = resolved.iter().find(|x| x.symbol.meta.name == "DERIVED").unwrap();
        assert_eq!(derived.symbol.pins.len(), 2);
        assert_eq!(derived.symbol.pins[0].pin_name, "IN");
        assert_eq!(derived.symbol.pins[1].pin_name, "OUT");
        assert_eq!(derived.component.signals.len(), 2);
    }
}
