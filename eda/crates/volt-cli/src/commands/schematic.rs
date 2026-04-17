//! `volt-eda schematic` subcommands.

use std::path::PathBuf;

use clap::Subcommand;
use uuid::Uuid;

use volt_core::common::*;
use volt_core::library::{Component, Symbol, SymbolPin, SymbolText};
use volt_core::project::*;

use super::project_io::{self, Result};

#[derive(Subcommand)]
pub enum SchematicCommands {
    /// Place a component symbol on the schematic
    Place {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        /// Schematic name (without .json)
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Component designator (e.g. "R1")
        #[arg(long)]
        component: String,
        /// Gate UUID or suffix (e.g. "A", "B") for multi-gate components. Omit for single-gate.
        #[arg(long)]
        gate: Option<String>,
        /// Position as grid coordinates "x,y" (grid = 2.54mm)
        #[arg(long)]
        grid: String,
        /// Rotation in degrees (0, 90, 180, 270)
        #[arg(long, default_value = "0")]
        rotation: f64,
        /// Mirror the symbol
        #[arg(long, default_value = "false")]
        mirror: bool,
    },
    /// Move a placed symbol to a new position
    Move {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Component designator to move
        #[arg(long)]
        component: String,
        /// New position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
    },
    /// Move or reset reference/value text fields on a placed symbol
    Field {
        #[command(subcommand)]
        command: SchematicFieldCommands,
    },
    /// Add a wire between two endpoints (pin or junction)
    Wire {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Net name
        #[arg(long)]
        net: String,
        /// From endpoint: "component:pin" (e.g. "R1:1" or "U1:OUT") or "x,y" for junction
        #[arg(long)]
        from: String,
        /// To endpoint: "component:pin" or "x,y" for junction
        #[arg(long)]
        to: String,
        /// Routing style: "manhattan" (default, right-angle segments) or "direct"
        #[arg(long, default_value = "manhattan")]
        route: String,
    },
    /// Add a net label at a position
    Label {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Net name
        #[arg(long)]
        net: String,
        /// Position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
        /// Rotation in degrees
        #[arg(long, default_value = "0")]
        rotation: f64,
    },
    /// Render the schematic to SVG
    Render {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        /// Output SVG file path
        #[arg(long)]
        output: PathBuf,
    },
    /// Auto-place all components and wire nets (replaces existing layout)
    Autoplace {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
    },
    /// Clean up an existing schematic layout
    Tidy {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
    },
}

#[derive(Subcommand)]
pub enum SchematicFieldCommands {
    /// Move a single field (name/reference or value) on a placed symbol
    Move {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        #[arg(long)]
        component: String,
        /// Field name: `name`/`reference` or `value`
        #[arg(long)]
        field: String,
        /// New absolute position as grid coordinates "x,y"
        #[arg(long)]
        grid: String,
    },
    /// Reset one field, or all fields, to the library default positions
    Reset {
        #[arg(long, default_value = ".")]
        project: PathBuf,
        #[arg(long, default_value = "main")]
        schematic: String,
        #[arg(long)]
        component: String,
        /// Optional field name: `name`/`reference` or `value`
        #[arg(long)]
        field: Option<String>,
    },
}

pub fn schematic_command(cmd: SchematicCommands) -> Result<()> {
    match cmd {
        SchematicCommands::Place {
            project, schematic, component, gate, grid, rotation, mirror,
        } => place_symbol(&project, &schematic, &component, gate.as_deref(), &grid, rotation, mirror),
        SchematicCommands::Move {
            project, schematic, component, grid,
        } => move_symbol(&project, &schematic, &component, &grid),
        SchematicCommands::Field { command } => match command {
            SchematicFieldCommands::Move {
                project,
                schematic,
                component,
                field,
                grid,
            } => move_symbol_field(&project, &schematic, &component, &field, &grid),
            SchematicFieldCommands::Reset {
                project,
                schematic,
                component,
                field,
            } => reset_symbol_field(&project, &schematic, &component, field.as_deref()),
        },
        SchematicCommands::Wire {
            project, schematic, net, from, to, route,
        } => add_wire(&project, &schematic, &net, &from, &to, &route),
        SchematicCommands::Label {
            project, schematic, net, grid, rotation,
        } => add_label(&project, &schematic, &net, &grid, rotation),
        SchematicCommands::Render {
            project, schematic, output,
        } => super::render::render_schematic(&project, &schematic, &output),
        SchematicCommands::Autoplace {
            project, schematic,
        } => super::autoplace::autoplace_schematic(&project, &schematic),
        SchematicCommands::Tidy {
            project, schematic,
        } => super::autoplace::tidy_schematic(&project, &schematic),
    }
}

fn parse_grid(s: &str) -> Result<Position> {
    let parts: Vec<&str> = s.split(',').collect();
    if parts.len() != 2 {
        return Err(format!("Invalid grid position '{}': expected 'x,y'", s).into());
    }
    let gx: f64 = parts[0].trim().parse()
        .map_err(|_| format!("Invalid grid x: '{}'", parts[0]))?;
    let gy: f64 = parts[1].trim().parse()
        .map_err(|_| format!("Invalid grid y: '{}'", parts[1]))?;
    // Grid units: 1 grid = 2.54mm
    Ok(Position::new(gx * 2.54, gy * 2.54))
}

fn place_symbol(
    project: &std::path::Path,
    sch_name: &str,
    comp_name: &str,
    gate_selector: Option<&str>,
    grid: &str,
    rotation: f64,
    mirror: bool,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let position = parse_grid(grid)?;

    // Find the component instance
    let comp_instance = circuit.components.iter()
        .find(|c| c.name == comp_name)
        .ok_or_else(|| format!("Component '{}' not found in circuit", comp_name))?;

    // Look up the library component to get gates
    let lib_comp: Component = project_io::read_library_element(
        project, "components", &comp_instance.lib_component,
    )?;

    let variant = lib_comp.variants.iter()
        .find(|v| v.uuid == comp_instance.lib_variant)
        .ok_or_else(|| format!("Variant not found for component '{}'", comp_name))?;

    if variant.gates.is_empty() {
        return Err(format!("No gates defined for component '{}'", comp_name).into());
    }

    // Collect already-placed gate UUIDs for this component
    let placed_gates: Vec<Uuid> = schematic.symbols.iter()
        .filter(|s| s.component == comp_instance.uuid)
        .map(|s| s.lib_gate)
        .collect();

    // Select the gate to place
    let gate = if let Some(selector) = gate_selector {
        // Try UUID first, then suffix match
        if let Ok(uuid) = selector.parse::<Uuid>() {
            variant.gates.iter().find(|g| g.uuid == uuid)
                .ok_or_else(|| format!("Gate UUID '{}' not found in component '{}'", selector, comp_name))?
        } else {
            variant.gates.iter().find(|g| g.suffix.eq_ignore_ascii_case(selector))
                .ok_or_else(|| format!("Gate suffix '{}' not found in component '{}'", selector, comp_name))?
        }
    } else {
        // Auto-select first unplaced gate
        variant.gates.iter()
            .find(|g| !placed_gates.contains(&g.uuid))
            .ok_or_else(|| format!(
                "All {} gate(s) of component '{}' are already placed on schematic '{}'",
                variant.gates.len(), comp_name, sch_name
            ))?
    };

    // Check that this specific gate isn't already placed
    if placed_gates.contains(&gate.uuid) {
        let suffix_info = if gate.suffix.is_empty() {
            String::new()
        } else {
            format!(" (gate {})", gate.suffix)
        };
        return Err(format!(
            "Gate{} of component '{}' is already placed on schematic '{}'",
            suffix_info, comp_name, sch_name
        ).into());
    }

    // Look up symbol to get text templates
    let lib_sym: Symbol = project_io::read_library_element(
        project, "symbols", &gate.symbol,
    )?;

    // Create schematic texts from the symbol's text templates, offset by position
    let texts: Vec<SchematicText> = lib_sym.texts.iter().map(|t| {
        SchematicText {
            uuid: new_uuid(),
            layer: t.layer,
            value: t.value.clone(),
            position: Position::new(
                position.x + t.position.x,
                position.y + t.position.y,
            ),
            rotation: Angle(t.rotation.0 + rotation),
            height: t.height,
            align: t.align,
            lock: t.lock,
        }
    }).collect();

    let sym_uuid = new_uuid();
    let symbol = SchematicSymbol {
        uuid: sym_uuid,
        component: comp_instance.uuid,
        lib_gate: gate.uuid,
        position,
        rotation: Angle(rotation),
        mirror,
        texts,
    };

    schematic.symbols.push(symbol);
    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "symbol": {
            "uuid": sym_uuid.to_string(),
            "component": comp_name,
            "position": { "x": position.x, "y": position.y },
            "grid": grid,
            "rotation": rotation,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn move_symbol(
    project: &std::path::Path,
    sch_name: &str,
    comp_name: &str,
    grid: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let new_pos = parse_grid(grid)?;

    let comp_instance = circuit.components.iter()
        .find(|c| c.name == comp_name)
        .ok_or_else(|| format!("Component '{}' not found in circuit", comp_name))?;

    let sym = schematic.symbols.iter_mut()
        .find(|s| s.component == comp_instance.uuid)
        .ok_or_else(|| format!("Component '{}' not placed on schematic '{}'", comp_name, sch_name))?;

    let old_pos = sym.position;
    let dx = new_pos.x - old_pos.x;
    let dy = new_pos.y - old_pos.y;

    sym.position = new_pos;

    // Move texts too
    for text in &mut sym.texts {
        text.position.x += dx;
        text.position.y += dy;
    }

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "moved": {
            "component": comp_name,
            "from": { "x": old_pos.x, "y": old_pos.y },
            "to": { "x": new_pos.x, "y": new_pos.y },
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn move_symbol_field(
    project: &std::path::Path,
    sch_name: &str,
    comp_name: &str,
    field: &str,
    grid: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;
    let new_pos = parse_grid(grid)?;
    let target_layer = field_layer(field)?;

    let comp_instance = circuit.components.iter()
        .find(|c| c.name == comp_name)
        .ok_or_else(|| format!("Component '{}' not found in circuit", comp_name))?;

    let sym = schematic.symbols.iter_mut()
        .find(|s| s.component == comp_instance.uuid)
        .ok_or_else(|| format!("Component '{}' not placed on schematic '{}'", comp_name, sch_name))?;

    let text = sym.texts.iter_mut()
        .find(|t| t.layer == target_layer)
        .ok_or_else(|| format!("Field '{}' not found on component '{}'", field, comp_name))?;

    let old_pos = text.position;
    text.position = new_pos;

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "field": {
            "component": comp_name,
            "field": canonical_field_name(target_layer),
            "from": { "x": old_pos.x, "y": old_pos.y },
            "to": { "x": new_pos.x, "y": new_pos.y },
            "grid": grid,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn reset_symbol_field(
    project: &std::path::Path,
    sch_name: &str,
    comp_name: &str,
    field: Option<&str>,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let comp_instance = circuit.components.iter()
        .find(|c| c.name == comp_name)
        .ok_or_else(|| format!("Component '{}' not found in circuit", comp_name))?;

    let sym = schematic.symbols.iter_mut()
        .find(|s| s.component == comp_instance.uuid)
        .ok_or_else(|| format!("Component '{}' not placed on schematic '{}'", comp_name, sch_name))?;

    let lib_comp: Component = project_io::read_library_element(
        project, "components", &comp_instance.lib_component,
    )?;
    let variant = lib_comp.variants.iter()
        .find(|v| v.uuid == comp_instance.lib_variant)
        .ok_or_else(|| format!("Variant not found for component '{}'", comp_name))?;
    let gate = variant.gates.iter()
        .find(|g| g.uuid == sym.lib_gate)
        .or_else(|| variant.gates.first())
        .ok_or_else(|| format!("No gates defined for component '{}'", comp_name))?;
    let lib_sym: Symbol = project_io::read_library_element(project, "symbols", &gate.symbol)?;

    let target_layers: Vec<Layer> = if let Some(field) = field {
        vec![field_layer(field)?]
    } else {
        vec![Layer::SchNames, Layer::SchValues]
    };

    let mut reset_fields = Vec::new();
    for layer in target_layers {
        let template = lib_sym.texts.iter()
            .find(|t| t.layer == layer)
            .ok_or_else(|| format!("No library template found for '{}' field on '{}'", canonical_field_name(layer), comp_name))?;
        apply_symbol_text_template(sym, template);
        reset_fields.push(canonical_field_name(layer));
    }

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "reset": {
            "component": comp_name,
            "fields": reset_fields,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn apply_symbol_text_template(sym: &mut SchematicSymbol, template: &SymbolText) {
    let rebuilt = SchematicText {
        uuid: sym.texts.iter().find(|t| t.layer == template.layer).map(|t| t.uuid).unwrap_or_else(new_uuid),
        layer: template.layer,
        value: template.value.clone(),
        position: Position::new(sym.position.x + template.position.x, sym.position.y + template.position.y),
        rotation: Angle(template.rotation.0 + sym.rotation.0),
        height: template.height,
        align: template.align,
        lock: template.lock,
    };

    if let Some(existing) = sym.texts.iter_mut().find(|t| t.layer == template.layer) {
        *existing = rebuilt;
    } else {
        sym.texts.push(rebuilt);
    }
}

fn field_layer(field: &str) -> Result<Layer> {
    match field.trim().to_ascii_lowercase().as_str() {
        "name" | "reference" => Ok(Layer::SchNames),
        "value" => Ok(Layer::SchValues),
        _ => Err(format!("Unknown field '{}'. Use 'name'/'reference' or 'value'", field).into()),
    }
}

fn canonical_field_name(layer: Layer) -> &'static str {
    match layer {
        Layer::SchNames => "name",
        Layer::SchValues => "value",
        _ => "field",
    }
}

fn find_symbol_pin<'a>(lib_sym: &'a Symbol, selector: &str) -> std::result::Result<&'a SymbolPin, String> {
    if let Some(pin) = lib_sym.pins.iter().find(|p| p.name == selector) {
        return Ok(pin);
    }

    let alias_matches: Vec<&SymbolPin> = lib_sym.pins.iter()
        .filter(|p| !p.pin_name.is_empty() && p.pin_name.eq_ignore_ascii_case(selector))
        .collect();

    match alias_matches.len() {
        1 => Ok(alias_matches[0]),
        0 => Err(format!(
            "Pin '{}' not found. Available: {}",
            selector,
            lib_sym.pins.iter().map(format_pin_display).collect::<Vec<_>>().join(", ")
        )),
        _ => Err(format!(
            "Pin name '{}' is ambiguous. Matches: {}",
            selector,
            alias_matches.iter().map(|p| format_pin_display(p)).collect::<Vec<_>>().join(", ")
        )),
    }
}

fn format_pin_display(pin: &SymbolPin) -> String {
    if pin.pin_name.is_empty() {
        pin.name.clone()
    } else {
        format!("{} ({})", pin.name, pin.pin_name)
    }
}

/// Parse an endpoint string: either "Component:Pin" or "x,y" (grid coords for junction)
fn parse_endpoint(
    s: &str,
    circuit: &Circuit,
    schematic: &Schematic,
    project: &std::path::Path,
) -> Result<(LineEndpoint, Option<Position>)> {
    if s.contains(':') {
        // Symbol pin: "R1:1"
        let parts: Vec<&str> = s.splitn(2, ':').collect();
        let comp_name = parts[0];
        let pin_name = parts[1];

        let comp_instance = circuit.components.iter()
            .find(|c| c.name == comp_name)
            .ok_or_else(|| format!("Component '{}' not found", comp_name))?;

        let sch_sym = schematic.symbols.iter()
            .find(|s| s.component == comp_instance.uuid)
            .ok_or_else(|| format!("Component '{}' not placed on schematic", comp_name))?;

        // Look up library component and symbol to find pin UUID
        let lib_comp: Component = project_io::read_library_element(
            project, "components", &comp_instance.lib_component,
        )?;
        let variant = lib_comp.variants.iter()
            .find(|v| v.uuid == comp_instance.lib_variant)
            .ok_or("Variant not found")?;
        let gate = variant.gates.first().ok_or("No gate")?;

        let lib_sym: Symbol = project_io::read_library_element(
            project, "symbols", &gate.symbol,
        )?;

        let pin = find_symbol_pin(&lib_sym, pin_name)
            .map_err(|e| format!("{e} on '{comp_name}'"))?;

        Ok((
            LineEndpoint::Symbol { symbol: sch_sym.uuid, pin: pin.uuid },
            None,
        ))
    } else {
        // Junction position: "x,y"
        let pos = parse_grid(s)?;
        Ok((
            LineEndpoint::Junction { junction: Uuid::nil() }, // placeholder, will be created
            Some(pos),
        ))
    }
}

/// Resolve a pin endpoint "Component:Pin" to its world position.
fn resolve_pin_position(
    comp_name: &str,
    pin_name: &str,
    circuit: &Circuit,
    schematic: &Schematic,
    project: &std::path::Path,
) -> Result<Position> {
    let comp_instance = circuit.components.iter()
        .find(|c| c.name == comp_name)
        .ok_or_else(|| format!("Component '{}' not found", comp_name))?;
    let sch_sym = schematic.symbols.iter()
        .find(|s| s.component == comp_instance.uuid)
        .ok_or_else(|| format!("Component '{}' not placed on schematic", comp_name))?;
    let lib_comp: Component = project_io::read_library_element(
        project, "components", &comp_instance.lib_component,
    )?;
    let variant = lib_comp.variants.iter()
        .find(|v| v.uuid == comp_instance.lib_variant)
        .ok_or("Variant not found")?;
    let gate = variant.gates.first().ok_or("No gate")?;
    let lib_sym: Symbol = project_io::read_library_element(
        project, "symbols", &gate.symbol,
    )?;
    let pin = find_symbol_pin(&lib_sym, pin_name)
        .map_err(|e| format!("{e} on '{comp_name}'"))?;

    // Transform pin position by symbol's position and rotation
    let rot_rad = sch_sym.rotation.0.to_radians();
    let cos_r = rot_rad.cos();
    let sin_r = rot_rad.sin();
    let px = pin.position.x;
    let py = pin.position.y;
    Ok(Position::new(
        sch_sym.position.x + px * cos_r - py * sin_r,
        sch_sym.position.y + px * sin_r + py * cos_r,
    ))
}

/// Resolve any endpoint string to a world position.
fn resolve_endpoint_position(
    s: &str,
    circuit: &Circuit,
    schematic: &Schematic,
    project: &std::path::Path,
) -> Result<Position> {
    if s.contains(':') {
        let parts: Vec<&str> = s.splitn(2, ':').collect();
        resolve_pin_position(parts[0], parts[1], circuit, schematic, project)
    } else {
        parse_grid(s)
    }
}

fn add_wire(
    project: &std::path::Path,
    sch_name: &str,
    net_name: &str,
    from_str: &str,
    to_str: &str,
    route_style: &str,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    // Find the net
    let net = circuit.nets.iter()
        .find(|n| n.name == net_name)
        .ok_or_else(|| format!("Net '{}' not found", net_name))?;

    // Resolve positions for Manhattan routing calculation
    let from_pos = resolve_endpoint_position(from_str, &circuit, &schematic, project)?;
    let to_pos = resolve_endpoint_position(to_str, &circuit, &schematic, project)?;

    let (from_ep, from_junc_pos) = parse_endpoint(from_str, &circuit, &schematic, project)?;
    let (to_ep, to_junc_pos) = parse_endpoint(to_str, &circuit, &schematic, project)?;

    // Find or create the net segment for this net
    let seg = if let Some(seg) = schematic.net_segments.iter_mut().find(|s| s.net == net.uuid) {
        seg
    } else {
        schematic.net_segments.push(SchematicNetSegment {
            uuid: new_uuid(),
            net: net.uuid,
            junctions: vec![],
            lines: vec![],
            labels: vec![],
        });
        schematic.net_segments.last_mut().unwrap()
    };

    // Create junctions for position-based endpoints
    let mut actual_from = from_ep;
    if let Some(pos) = from_junc_pos {
        let junc_uuid = new_uuid();
        seg.junctions.push(Junction { uuid: junc_uuid, position: pos });
        actual_from = LineEndpoint::Junction { junction: junc_uuid };
    }
    let mut actual_to = to_ep;
    if let Some(pos) = to_junc_pos {
        let junc_uuid = new_uuid();
        seg.junctions.push(Junction { uuid: junc_uuid, position: pos });
        actual_to = LineEndpoint::Junction { junction: junc_uuid };
    }

    let use_manhattan = route_style == "manhattan";
    let dx = (to_pos.x - from_pos.x).abs();
    let dy = (to_pos.y - from_pos.y).abs();
    let aligned = dx < 0.01 || dy < 0.01; // Already horizontal or vertical

    let mut line_uuids = Vec::new();

    if use_manhattan && !aligned {
        // Create a bend junction and two wire segments
        // Choose bend direction: horizontal-first if dx >= dy, vertical-first otherwise
        let bend_pos = if dx >= dy {
            // Horizontal first: go to (to_x, from_y), then vertical to target
            Position::new(to_pos.x, from_pos.y)
        } else {
            // Vertical first: go to (from_x, to_y), then horizontal to target
            Position::new(from_pos.x, to_pos.y)
        };

        // Snap bend to grid (2.54mm)
        let grid = 2.54;
        let bend_snapped = Position::new(
            (bend_pos.x / grid).round() * grid,
            (bend_pos.y / grid).round() * grid,
        );

        let bend_uuid = new_uuid();
        seg.junctions.push(Junction { uuid: bend_uuid, position: bend_snapped });
        let bend_ep = LineEndpoint::Junction { junction: bend_uuid };

        // Wire 1: from → bend
        let line1_uuid = new_uuid();
        seg.lines.push(SchematicLine {
            uuid: line1_uuid,
            width: 0.15875,
            from: actual_from,
            to: bend_ep.clone(),
        });
        line_uuids.push(line1_uuid);

        // Wire 2: bend → to
        let line2_uuid = new_uuid();
        seg.lines.push(SchematicLine {
            uuid: line2_uuid,
            width: 0.15875,
            from: bend_ep,
            to: actual_to,
        });
        line_uuids.push(line2_uuid);
    } else {
        // Direct wire (single segment)
        let line_uuid = new_uuid();
        seg.lines.push(SchematicLine {
            uuid: line_uuid,
            width: 0.15875,
            from: actual_from,
            to: actual_to,
        });
        line_uuids.push(line_uuid);
    }

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "wire": {
            "uuids": line_uuids.iter().map(|u| u.to_string()).collect::<Vec<_>>(),
            "net": net_name,
            "from": from_str,
            "to": to_str,
            "route": route_style,
            "segments": line_uuids.len(),
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}

fn add_label(
    project: &std::path::Path,
    sch_name: &str,
    net_name: &str,
    grid: &str,
    rotation: f64,
) -> Result<()> {
    project_io::ensure_project(project)?;
    let circuit = project_io::read_circuit(project)?;
    let mut schematic = project_io::read_schematic(project, sch_name)?;

    let position = parse_grid(grid)?;

    let net = circuit.nets.iter()
        .find(|n| n.name == net_name)
        .ok_or_else(|| format!("Net '{}' not found", net_name))?;

    // Find or create net segment
    let seg = if let Some(seg) = schematic.net_segments.iter_mut().find(|s| s.net == net.uuid) {
        seg
    } else {
        schematic.net_segments.push(SchematicNetSegment {
            uuid: new_uuid(),
            net: net.uuid,
            junctions: vec![],
            lines: vec![],
            labels: vec![],
        });
        schematic.net_segments.last_mut().unwrap()
    };

    let label_uuid = new_uuid();
    seg.labels.push(NetLabel {
        uuid: label_uuid,
        position,
        rotation: Angle(rotation),
        mirror: false,
    });

    project_io::write_schematic(project, sch_name, &schematic)?;

    let result = serde_json::json!({
        "status": "ok",
        "label": {
            "uuid": label_uuid.to_string(),
            "net": net_name,
            "position": { "x": position.x, "y": position.y },
            "grid": grid,
        }
    });
    println!("{}", serde_json::to_string_pretty(&result)?);
    Ok(())
}
